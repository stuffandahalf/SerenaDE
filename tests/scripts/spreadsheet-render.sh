#!/bin/sh
# Third-batch driver: prove Spreadsheet renders its window on host.
#
# Brings up WindowServer + Spreadsheet with Config/Clipboard/Launch as
# regular services and WebContent as a broker service (the app's declared
# service dependency -- its formula help view is an out-of-process web view;
# the broker takes over an *accepted* client socket, one process per
# connection). With no file argument Spreadsheet opens a fresh empty workbook
# -- no network or files needed. The check is functional (--expect-window),
# not a pixel golden: the grid's column/row headers are deterministic but the
# web view dependency makes this session heavier than the golden driver's
# plain three-service shape.
#
# Usage: spreadsheet-render.sh <launcher> <res> <screenshot> <logdir> \
#                              <window-server> <spreadsheet> <config> <clipboard> \
#                              <launch> <webcontent>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; spreadsheet="$6"; cfg="$7"; clip="$8"; launchsrv="$9"
shift 9
webcontent="$1"

base=$(mktemp -d /tmp/serenade-spreadsheet-render.XXXXXX)
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
    --expect-window 0.15 \
    "$ws" "$spreadsheet"
