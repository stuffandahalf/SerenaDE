#!/bin/sh
# M7 driver: prove SystemMonitor renders its process list on host.
#
# Brings up WindowServer + SystemMonitor with Config/Clipboard/Launch as
# regular services. The launcher generates a host /sys/kernel data shim in
# <logdir>/sysfs (processes from /proc, memstat/cpuinfo via sysconf, df via
# statvfs, net adapters via getifaddrs) and points $SERENITY_SYS at it; LibCore
# remaps the app's read-only /sys/kernel/* opens onto that directory. The check
# is functional (--expect-window): the process table fills from live /proc
# data, so no pixel golden is involved.
#
# Usage: systemmonitor-render.sh <launcher> <res> <screenshot> <logdir> \
#                                <window-server> <systemmonitor> <config> \
#                                <clipboard> <launch>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; sm="$6"; cfg="$7"; clip="$8"; launchsrv="$9"

base=$(mktemp -d /tmp/serenade-systemmonitor-render.XXXXXX)
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
    "$ws" "$sm"
