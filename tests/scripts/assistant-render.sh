#!/bin/sh
# Third-batch driver: prove Assistant renders its popup on host.
#
# Brings up WindowServer + Assistant with Config/Clipboard/Launch as regular
# services. Assistant is a search-launcher popup (no file argument); its
# providers only run when a query is typed, so nothing beyond the window
# server is exercised at startup. The lockfile under /tmp/lock is created by
# Core::LockFile itself (it mkdirs its parent), and the jail-mode entry point
# it calls before exec is a no-op on host (patch 0045). The popup covers only
# ~2% of the desktop, so the non-background fraction threshold is low (an
# empty desktop measures ~0).
#
# Usage: assistant-render.sh <launcher> <res> <screenshot> <logdir> \
#                            <window-server> <assistant> <config> <clipboard> <launch>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; assistant="$6"; cfg="$7"; clip="$8"; launchsrv="$9"

base=$(mktemp -d /tmp/serenade-assistant-render.XXXXXX)
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
    --expect-window 0.01 \
    "$ws" "$assistant"
