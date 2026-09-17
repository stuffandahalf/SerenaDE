#!/bin/sh
# App-completion driver: prove Mail renders its window on host.
#
# Brings up WindowServer + Mail with Config/Clipboard/Launch as regular
# services and WebContent as a broker service (Mail's out-of-process web view
# connects to it at construction and aborts if the service is missing; like
# all single-client services it takes over an *accepted* client socket, one
# process per connection). With no account configured Mail shows its empty
# inbox UI -- no IMAP server or network needed -- so this is fully headless.
# The check is functional (--expect-window), not a pixel golden: the web view
# area is rendered by the WebContent process and its empty state can drift
# between hosts.
#
# Usage: mail-render.sh <launcher> <res> <screenshot> <logdir> \
#                       <window-server> <mail> <config> <clipboard> <launch> <webcontent>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; mail="$6"; cfg="$7"; clip="$8"; launchsrv="$9"
shift 9
webcontent="$1"

base=$(mktemp -d /tmp/serenade-mail-render.XXXXXX)
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
    --expect-window 0.25 \
    "$ws" "$mail"
