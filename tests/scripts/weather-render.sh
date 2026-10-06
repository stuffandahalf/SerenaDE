#!/bin/sh
# Third-batch driver: prove Weather renders its window on host.
#
# Brings up WindowServer + Weather with Config/Clipboard/Launch as regular
# services and RequestServer as a broker service (Weather's
# Protocol::RequestClient connects at startup -- before the first search --
# and an unhandled failure there would kill the app; the broker takes over an
# *accepted* client socket, one process per connection). The OpenWeatherMap
# API key is pre-seeded in the session's Weather config: without it Weather
# shows a password dialog that can never be answered headless. No network
# traffic happens until a search is typed, so the window renders offline.
# The check is functional (--expect-window), not a pixel golden: the content
# area is an empty list of weather entries.
#
# Usage: weather-render.sh <launcher> <res> <screenshot> <logdir> \
#                          <window-server> <weather> <config> <clipboard> \
#                          <launch> <request>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; weather="$6"; cfg="$7"; clip="$8"; launchsrv="$9"
shift 9
request="$1"

base=$(mktemp -d /tmp/serenade-weather-render.XXXXXX)
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

# Pre-seed the API key so Weather skips its password dialog (which a headless
# session can never answer). The value is never used: no request is made.
cat > "$base/home/.config/Weather.ini" <<'EOF'
[OpenWeatherMap]
APIKey=serenade-test-key
EOF

# Drop state from previous runs so the probe only ever sees pixels (and a pid)
# from this session, and stale logs never masquerade as this run's diagnostics.
rm -f "$shot" "$logdir/serenade-windowserver.pid" "$logdir"/serenade-*.log

exec "$launcher" \
    --res "$res" \
    --screenshot "$shot" \
    --delay 6000 \
    --log-dir "$logdir" \
    --home "$base/home" \
    --service /tmp/session/0/portal/config="$cfg" \
    --service /tmp/session/0/portal/clipboard="$clip" \
    --service /tmp/session/0/portal/launch="$launchsrv" \
    --broker-service /tmp/session/0/portal/request="$request" \
    --expect-window 0.25 \
    "$ws" "$weather"
