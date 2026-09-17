#!/bin/sh
# App-completion driver: prove ImageViewer opens a real file through
# FileSystemAccessServer on host.
#
# Brings up the real session -- WindowServer + ImageViewer <file> with
# ConfigServer, Clipboard and LaunchServer as regular services and
# FileSystemAccessServer + ImageDecoder as broker services (they take over an
# *accepted* client socket on fd 3, one process per connection, like
# WebContent) -- using a generated solid-red test image. The CLI file argument
# goes through
# request_file_read_only_approved(), which auto-approves (no prompt), so this
# is fully headless: the assertion is that the screenshot contains the red
# image pixels, i.e. the full path worked: ImageViewer -> FileSystemAccessServer
# (IPC) -> file opened -> decoded -> rendered.
#
# Skips (exit 77) when python3 is unavailable.
#
# Usage: imageviewer-open.sh <launcher> <res> <screenshot> <logdir> \
#                            <window-server> <imageviewer> <config> <clipboard> \
#                            <launch> <filesystemaccess> <image>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; imageviewer="$6"; config="$7"; clipboard="$8"; launchsrv="$9"
shift 9
fas="$1"; image="$2"

if ! command -v python3 >/dev/null 2>&1; then
    echo "imageviewer-open: python3 not found, skipping"
    exit 77
fi

base=$(mktemp -d /tmp/serenade-imageviewer-open.XXXXXX)
cleanup() {
    rm -rf "$base"
}
trap cleanup EXIT INT TERM

mkdir -p "$base/img" "$base/home/.config" "$base/home/.local/share" "$base/runtime"

# Pin the XDG dirs to the seeded home: StandardPaths prefers them over $HOME,
# and an inherited value (CI images set them) would break config lookups.
export XDG_RUNTIME_DIR="$base/runtime"
export XDG_CONFIG_HOME="$base/home/.config"
export XDG_DATA_HOME="$base/home/.local/share"

# Seed the home from Base/home/anon (harmless if unused; keeps this script
# consistent with browser-load.sh).
anon=$(dirname "$res")/home/anon
if [ ! -d "$anon" ]; then
    echo "imageviewer-open: seed home not found: $anon"
    exit 1
fi
cp -rT "$anon" "$base/home"

# Drop state from previous runs so the probe only ever sees pixels (and a pid)
# from this session, and stale logs never masquerade as this run's diagnostics
# (the launcher only truncates the log files it opens itself).
rm -f "$shot" "$logdir/serenade-windowserver.pid" "$logdir"/serenade-*.log

# A 100x100 solid red PNG, generated with the stdlib (no binary assets).
python3 - "$base/img/red.png" <<'EOF'
import struct
import sys
import zlib

def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

w = h = 100
raw = b"".join(b"\x00" + b"\xff\x00\x00" * w for _ in range(h))
png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(raw))
       + chunk(b"IEND", b""))
open(sys.argv[1], "wb").write(png)
EOF

# Pixel probe: prints the count of red pixels (sampled every 2nd row/col).
# Prints 0 if the PNG is missing or mid-write, so callers can simply retry.
cat > "$base/probe.py" <<'EOF'
import struct
import sys
import zlib

def load_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    pos = 8
    width = height = bitdepth = colortype = None
    idat = b""
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            width, height, bitdepth, colortype = struct.unpack(">IIBB", chunk[:10])
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break
        pos += 12 + length
    if bitdepth != 8 or colortype not in (0, 2, 6):
        return None
    channels = {0: 1, 2: 3, 6: 4}[colortype]
    raw = zlib.decompress(idat)
    stride = width * channels
    out = bytearray(height * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(height):
        f = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if f == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, channels, bytes(out)

def main():
    loaded = load_png(sys.argv[1])
    if not loaded:
        print(0)
        return
    w, h, ch, px = loaded
    red = 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            o = (y * w + x) * ch
            # Grayscale (ch == 1) pixels can never satisfy the red test.
            if ch > 1 and px[o] > 150 and px[o + 1] < 100 and px[o + 2] < 100:
                red += 1
    print(red)

try:
    main()
except Exception:
    print(0)
EOF

# Run the session in the background with a long deadline and poll for the
# rendered image (same pattern as browser-load.sh: a cold CI runner can take
# well over a fixed delay to start the app). SIGUSR1 makes WindowServer dump
# its front buffer to the screenshot path; once the red image is visible, end
# the session.
"$launcher" \
    --res "$res" \
    --screenshot "$shot" \
    --delay 60000 \
    --log-dir "$logdir" \
    --home "$base/home" \
    --service /tmp/session/0/portal/config="$config" \
    --service /tmp/session/0/portal/clipboard="$clipboard" \
    --service /tmp/session/0/portal/launch="$launchsrv" \
    --broker-service /tmp/session/0/portal/filesystemaccess="$fas" \
    --broker-service /tmp/session/0/portal/image="$image" \
    "$ws" "$imageviewer" "$base/img/red.png" &
launcher_pid=$!

dump_logs() {
    echo "imageviewer-open: session logs:"
    for f in "$logdir"/serenade-app.log "$logdir"/serenade-service-*.log "$logdir"/serenade-broker-*.log; do
        [ -f "$f" ] || continue
        echo "--- $f ---"
        cat "$f"
    done
    echo "--- environment ---"
    env | grep -E "^(HOME|USER|XDG_|SERENITY_RES|WINDOW_SERVER)" | sort || true
}

# Wait for the launcher to record WindowServer's pid (written once WS is up).
ws_pid=""
i=0
while [ -z "$ws_pid" ] && kill -0 "$launcher_pid" 2>/dev/null; do
    i=$((i + 1))
    [ "$i" -ge 100 ] && break
    if [ -s "$logdir/serenade-windowserver.pid" ]; then
        ws_pid=$(cat "$logdir/serenade-windowserver.pid")
    fi
    sleep 0.2
done
if [ -z "$ws_pid" ]; then
    echo "imageviewer-open: WindowServer did not start (launcher may have died)"
    dump_logs
    kill -TERM "$launcher_pid" 2>/dev/null || true
    exit 1
fi

red=0
deadline=$(( $(date +%s) + 60 ))
while [ "$(date +%s)" -lt "$deadline" ] && kill -0 "$launcher_pid" 2>/dev/null; do
    kill -USR1 "$ws_pid" 2>/dev/null || true
    sleep 2
    red=$(python3 "$base/probe.py" "$shot" 2>/dev/null) || red=0
    [ "$red" -ge 500 ] && break
done

kill -TERM "$launcher_pid" 2>/dev/null || true
wait "$launcher_pid" 2>/dev/null || true

# The 100x100 image samples to ~2500 red px; the only other red on screen is a
# small titlebar UI element (~175 sampled), so 500 is a wide margin.
if [ "$red" -lt 500 ]; then
    echo "imageviewer-open: image not rendered in screenshot (red px sampled: $red)"
    dump_logs
    exit 1
fi
echo "imageviewer-open: OK, image opened and rendered (red px sampled: $red)"
