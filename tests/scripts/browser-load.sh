#!/bin/sh
# M6 browser driver: prove that Browser actually loads a web page over HTTP.
#
# Brings up the real session -- WindowServer + Browser with ConfigServer,
# Clipboard, LaunchServer and SQLServer as regular services, and WebContent,
# RequestServer and ImageDecoder as broker (one instance per connection)
# services -- pointed at a local python http.server serving a marker page.
# The assertions are that the server received the page's GET with a 200
# response and that the screenshot contains the page's red block plus text
# pixels, i.e. the full path works: Browser -> WebContent (IPC) ->
# RequestServer (IPC) -> real TCP/HTTP fetch -> HTML/CSS rendering.
#
# Skips (exit 77) when python3 is unavailable.
#
# Usage: browser-load.sh <launcher> <res> <screenshot> <logdir> \
#                        <window-server> <browser> <config> <clipboard> \
#                        <launch> <sql> <webcontent> <request> <image>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; browser="$6"; config="$7"; clipboard="$8"; launchsrv="$9"
shift 9
sql="$1"; webcontent="$2"; request="$3"; image="$4"

if ! command -v python3 >/dev/null 2>&1; then
    echo "browser-load: python3 not found, skipping"
    exit 77
fi

base=$(mktemp -d /tmp/serenade-browser-load.XXXXXX)
http_pid=0
cleanup() {
    [ "$http_pid" != 0 ] && kill "$http_pid" 2>/dev/null || true
    rm -rf "$base"
}
trap cleanup EXIT INT TERM

mkdir -p "$base/web" "$base/home" "$base/runtime"

# Isolate the browser's singleton state (Ladybird.pid/.socket live in
# $XDG_RUNTIME_DIR) in a private directory so runs never collide with each
# other or with a real user session.
export XDG_RUNTIME_DIR="$base/runtime"

# Browser unconditionally reads ~/.config/BrowserContentFilters.txt and
# BrowserAutoplayAllowlist.txt at startup; seed the home from Base/home/anon.
anon=$(dirname "$res")/home/anon
if [ -d "$anon" ]; then
    cp -rT "$anon" "$base/home"
fi

cat > "$base/web/index.html" <<'EOF'
<!DOCTYPE html>
<html><head><title>Serenade M6</title></head>
<body style="margin:0;background:white">
<div style="width:400px;height:250px;background:red"></div>
<h1>SERENADE_M6_BROWSER_OK</h1>
<p>The browser loaded this page over HTTP.</p>
<p>WindowServer, WebContent, RequestServer and ImageDecoder all participated in rendering this document.</p>
<p>If you can read these lines, the full fetch-and-render path is working end to end.</p>
<p>SerenityOS running natively on a host system through the SerenaDE shim.</p>
</body></html>
EOF

port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')
python3 -m http.server "$port" --bind 127.0.0.1 --directory "$base/web" \
    > "$base/httpd.log" 2>&1 &
http_pid=$!

# Wait until the server answers before starting the session.
i=0
until curl -s -o /dev/null "http://127.0.0.1:$port/index.html"; do
    i=$((i + 1))
    [ "$i" -ge 50 ] && { echo "browser-load: http server did not come up"; exit 1; }
    sleep 0.2
done

"$launcher" \
    --res "$res" \
    --screenshot "$shot" \
    --delay 15000 \
    --log-dir "$logdir" \
    --home "$base/home" \
    --service /tmp/session/0/portal/config="$config" \
    --service /tmp/session/0/portal/clipboard="$clipboard" \
    --service /tmp/session/0/portal/launch="$launchsrv" \
    --service /tmp/session/0/portal/sql="$sql" \
    --broker-service /tmp/session/0/portal/webcontent="$webcontent" \
    --broker-service /tmp/session/0/portal/request="$request" \
    --broker-service /tmp/session/0/portal/image="$image" \
    "$ws" "$browser" "http://127.0.0.1:$port/index.html"

kill "$http_pid" 2>/dev/null || true
wait "$http_pid" 2>/dev/null || true
http_pid=0

# Assert 1: the page was actually fetched over HTTP (and not served from
# anywhere else or silently failed).
if ! grep -q "GET /index.html" "$base/httpd.log" || ! grep "GET /index.html" "$base/httpd.log" | grep -q " 200 "; then
    echo "browser-load: http server log does not show a 200 for the page:"
    cat "$base/httpd.log"
    exit 1
fi

# Assert 2: the screenshot shows the rendered page. The marker page has a
# 400x250 red block (the only large red area on screen) and text lines; an
# error page or blank window has neither.
python3 - "$shot" <<'EOF'
import struct
import sys
import zlib

def load_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("browser-load: not a PNG: %s" % path)
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
    # The WindowServer's PNG writer picks the color type from the *content*:
    # a screen with no colored window encodes as grayscale (colortype 0), a
    # rendered page as truecolor (2) or truecolor+alpha (6).
    if bitdepth != 8 or colortype not in (0, 2, 6):
        raise SystemExit("browser-load: unsupported PNG format (bitdepth=%s colortype=%s)" % (bitdepth, colortype))
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

w, h, ch, px = load_png(sys.argv[1])

def at(x, y):
    o = (y * w + x) * ch
    if ch == 1:
        v = px[o]
        return v, v, v
    return px[o], px[o + 1], px[o + 2]

red = 0
dark = 0
for y in range(0, h, 2):
    for x in range(0, w, 2):
        r, g, b = at(x, y)
        if r > 150 and g < 100 and b < 100:
            red += 1
        elif 155 <= y <= 655 and 110 <= x <= 820 and r < 100 and g < 100 and b < 100:
            dark += 1

# The red block is 400x250 = 100k px; sampled every 2nd row/col that is
# ~25k. The only other red on screen is a ~700 px UI element, so 3000 is a
# wide margin either way.
if red < 3000:
    print("browser-load: red marker block not found in screenshot (red px sampled: %d)" % red)
    sys.exit(1)
# A rendered marker page has several lines of text (~1500+ sampled dark px);
# the "Load failed" error page has one short line (~100). 500 separates them
# decisively.
if dark < 500:
    print("browser-load: no text pixels in page area (dark px sampled: %d)" % dark)
    sys.exit(1)
print("browser-load: OK, page rendered (red px sampled: %d, text px sampled: %d)" % (red, dark))
EOF
