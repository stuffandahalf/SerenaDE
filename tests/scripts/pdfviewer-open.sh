#!/bin/sh
# Third-batch driver: prove PDFViewer opens a real PDF through
# FileSystemAccessServer on host.
#
# Brings up the real session -- WindowServer + PDFViewer <file> with
# ConfigServer, Clipboard and LaunchServer as regular services and
# FileSystemAccessServer as a broker service (it takes over an *accepted*
# client socket on fd 3, one process per connection) -- using a pinned test
# fixture from Tests/LibPDF. The CLI file argument goes through
# request_file_read_only_approved(), which auto-approves (no prompt), so this
# is fully headless.
#
# The assertion is a pixel probe: colorspaces.pdf renders colored swatches
# plus dark graphics on the first page, so a rendered document area contains
# many non-page pixels; an empty viewer (file never opened) does not. The
# probe samples the central region of the screenshot -- below the menubar --
# and counts pixels that are either dark or strongly saturated.
#
# Skips (exit 77) when python3 is unavailable.
#
# Usage: pdfviewer-open.sh <launcher> <res> <screenshot> <logdir> \
#                          <window-server> <pdfviewer> <config> <clipboard> \
#                          <launch> <filesystemaccess> <pdf>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; pdfviewer="$6"; config="$7"; clipboard="$8"; launchsrv="$9"
shift 9
fas="$1"; pdf="$2"

if ! command -v python3 >/dev/null 2>&1; then
    echo "pdfviewer-open: python3 not found, skipping"
    exit 77
fi

if [ ! -f "$pdf" ]; then
    echo "pdfviewer-open: fixture not found: $pdf"
    exit 1
fi

base=$(mktemp -d /tmp/serenade-pdfviewer-open.XXXXXX)
cleanup() {
    rm -rf "$base"
}
trap cleanup EXIT INT TERM

mkdir -p "$base/home/.config" "$base/home/.local/share" "$base/runtime"

# Pin the XDG dirs to the session home: StandardPaths prefers them over $HOME,
# and an inherited value (CI images set them) would break config lookups.
export XDG_RUNTIME_DIR="$base/runtime"
export XDG_CONFIG_HOME="$base/home/.config"
export XDG_DATA_HOME="$base/home/.local/share"

# Drop state from previous runs so the probe only ever sees pixels (and a pid)
# from this session, and stale logs never masquerade as this run's diagnostics.
rm -f "$shot" "$logdir/serenade-windowserver.pid" "$logdir"/serenade-*.log

# Pixel probe: counts content pixels (dark or saturated) in the central
# region of the screenshot -- the document area, below the menubar. Prints 0
# if the PNG is missing or mid-write, so callers can simply retry.
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
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb < pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                line[i] = (line[i] + (a + b) // 2) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, channels, bytes(out)

def main():
    loaded = load_png(sys.argv[1])
    if not loaded:
        print(0)
        return
    w, h, ch, px = loaded
    content = 0
    # Central region: x in [25%, 75%], y in [20%, 85%] -- the document area,
    # clear of the titlebar and menubar.
    for y in range(h // 5, h * 17 // 20):
        for x in range(w // 4, w * 3 // 4):
            o = (y * w + x) * ch
            r, g, b = px[o], px[o + 1], px[o + 2]
            if r < 100 and g < 100 and b < 100:
                content += 1
            elif max(r, g, b) - min(r, g, b) > 60:
                content += 1
    print(content)

try:
    main()
except Exception:
    print(0)
EOF

# Run the session in the background with a long deadline and poll for the
# rendered page (same pattern as imageviewer-open.sh: a cold CI runner can
# take well over a fixed delay to start the app). SIGUSR1 makes WindowServer
# dump its front buffer to the screenshot path; once the page content is
# visible, end the session.
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
    "$ws" "$pdfviewer" "$pdf" &
launcher_pid=$!

dump_logs() {
    echo "pdfviewer-open: session logs:"
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
    echo "pdfviewer-open: WindowServer did not start (launcher may have died)"
    dump_logs
    kill -TERM "$launcher_pid" 2>/dev/null || true
    exit 1
fi

content=0
deadline=$(( $(date +%s) + 60 ))
while [ "$(date +%s)" -lt "$deadline" ] && kill -0 "$launcher_pid" 2>/dev/null; do
    kill -USR1 "$ws_pid" 2>/dev/null || true
    sleep 2
    content=$(python3 "$base/probe.py" "$shot" 2>/dev/null) || content=0
    [ "$content" -ge 500 ] && break
done

kill -TERM "$launcher_pid" 2>/dev/null || true
wait "$launcher_pid" 2>/dev/null || true

# colorspaces.pdf's first page is dense with colored swatches and dark
# graphics; even at the viewer's fit scale the central region holds thousands
# of content pixels. An empty viewer shows a blank page: near zero.
if [ "$content" -lt 500 ]; then
    echo "pdfviewer-open: PDF not rendered in screenshot (content px sampled: $content)"
    dump_logs
    exit 1
fi
echo "pdfviewer-open: OK, PDF opened and rendered (content px sampled: $content)"
