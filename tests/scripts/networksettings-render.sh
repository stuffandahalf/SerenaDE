#!/bin/sh
# M7 driver: prove NetworkSettings renders its adapter list on host.
#
# Brings up WindowServer + NetworkSettings with Config/Clipboard/Launch as
# regular services. The launcher's /sys/kernel shim supplies net/adapters from
# getifaddrs (plus empty tcp/udp connection lists); the app reads those via
# LibCore's $SERENITY_SYS remap and renders one row per non-loopback adapter.
# Note: the app exits(1) with a message box if every adapter is named "loop" --
# CI runners have at least one real interface, so this passes there. The check
# is functional (--expect-window), not a pixel golden: the adapter list depends
# on the host's network configuration.
#
# Usage: networksettings-render.sh <launcher> <res> <screenshot> <logdir> \
#                                  <window-server> <networksettings> <config> \
#                                  <clipboard> <launch>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; ns="$6"; cfg="$7"; clip="$8"; launchsrv="$9"

base=$(mktemp -d /tmp/serenade-networksettings-render.XXXXXX)
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
    --expect-window 0.15 \
    "$ws" "$ns"
