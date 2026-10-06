#!/bin/sh
# Third-batch driver: prove Presenter renders its window on host.
#
# Brings up WindowServer + Presenter with Config/Clipboard/Launch as regular
# services and FileSystemAccessServer + WebContent as broker services (they
# take over an *accepted* client socket, one process per connection). The
# out-of-process web view PresenterWidget constructs at startup connects to
# WebContent immediately and aborts if the service is missing; with no
# .presenter argument it shows its empty slide view. The check is functional
# (--expect-window), not a pixel golden: an empty presentation's chrome is
# thin and its exact layout can drift between hosts.
#
# Usage: presenter-render.sh <launcher> <res> <screenshot> <logdir> \
#                            <window-server> <presenter> <config> <clipboard> \
#                            <launch> <filesystemaccess> <webcontent>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; presenter="$6"; cfg="$7"; clip="$8"; launchsrv="$9"
shift 9
fas="$1"; webcontent="$2"

base=$(mktemp -d /tmp/serenade-presenter-render.XXXXXX)
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
    --broker-service /tmp/session/0/portal/filesystemaccess="$fas" \
    --broker-service /tmp/session/0/portal/webcontent="$webcontent" \
    --expect-window 0.15 \
    "$ws" "$presenter"
