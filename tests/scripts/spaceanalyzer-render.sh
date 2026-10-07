#!/bin/sh
# M7 driver: prove SpaceAnalyzer renders its treemap on host.
#
# Brings up WindowServer + SpaceAnalyzer with Config/Clipboard/Launch as
# regular services. The launcher's /sys/kernel shim supplies df with a single
# mount (the session home, real statvfs stats), and the app walks that tree
# for its treemap -- so the driver seeds the session home with an 8 MiB file
# and a small text file first, giving the map non-trivial content. The check
# is functional (--expect-window): block counts depend on the host filesystem.
#
# Usage: spaceanalyzer-render.sh <launcher> <res> <screenshot> <logdir> \
#                                <window-server> <spaceanalyzer> <config> \
#                                <clipboard> <launch>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; sa="$6"; cfg="$7"; clip="$8"; launchsrv="$9"

base=$(mktemp -d /tmp/serenade-spaceanalyzer-render.XXXXXX)
cleanup() {
    rm -rf "$base"
}
trap cleanup EXIT INT TERM

mkdir -p "$base/home/.config" "$base/home/.local/share" "$base/runtime" "$base/home/Documents"

# Seed the session home (the shim's single "mount") so the treemap has real
# entries to lay out: one 8 MiB file and one small text file.
dd if=/dev/zero of="$base/home/bigfile.bin" bs=1M count=8 status=none
printf 'serenade space analyzer fixture\n' > "$base/home/Documents/note.txt"

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
    "$ws" "$sa"
