#!/bin/sh
# Third-batch driver: prove Welcome renders its window on host.
#
# Brings up WindowServer + Welcome with Config/Clipboard/Launch as regular
# services and WebContent as a broker service (Welcome's news area is an
# out-of-process web view created at construction; the broker takes over an
# *accepted* client socket, one process per connection). The tips file
# (/usr/share/Welcome/tips.txt) does not exist on host, so the tip label
# shows its error text -- the window still renders. The check is functional
# (--expect-window), not a pixel golden: the web view area is rendered by the
# WebContent process and can drift between hosts.
#
# Usage: welcome-render.sh <launcher> <res> <screenshot> <logdir> \
#                          <window-server> <welcome> <config> <clipboard> \
#                          <launch> <webcontent>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; welcome="$6"; cfg="$7"; clip="$8"; launchsrv="$9"
shift 9
webcontent="$1"

base=$(mktemp -d /tmp/serenade-welcome-render.XXXXXX)
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

exec "$launcher" \
    --res "$res" \
    --screenshot "$shot" \
    --delay 6000 \
    --log-dir "$logdir" \
    --home "$base/home" \
    --service /tmp/session/0/portal/config="$cfg" \
    --service /tmp/session/0/portal/clipboard="$clip" \
    --service /tmp/session/0/portal/launch="$launchsrv" \
    --broker-service /tmp/session/0/portal/webcontent="$webcontent" \
    --expect-window 0.08 \
    "$ws" "$welcome"
