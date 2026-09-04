#!/bin/sh
# M4 PixelPaint empty-window driver.
#
# Launches PixelPaint with no file argument so it renders its default (empty) document:
# the toolbox, a 510x356 transparent canvas, the layer list, palette and statusbar. No
# image is opened, so FileSystemAccessClient::the() is never called and no
# FileSystemAccessServer is needed at runtime -- only the standard config/launch/clipboard
# services run.
#
# PixelPaint restores/saves its window size+position from config (restore_size_and_position /
# save_size_and_position_on_close), so a fresh per-run $HOME (--home) keeps the placement
# deterministic: always the default 800x520 at WindowServer's fixed initial position. The
# render has no host-dependent content (no clock, directory listing or timezone), so it is
# golden-able like Settings/ImageViewer.
#
# Usage: pixelpaint-empty.sh <launcher> <res> <screenshot> <logdir> <golden> \
#                            <window-server> <pixelpaint> <config> <clipboard> <launch>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"; golden="$5"
ws="$6"; pixelpaint="$7"; cfg="$8"; clip="$9"; launch="${10}"

base=/tmp/serenade-pixelpaint
home="$base/home"
rm -rf "$base"
mkdir -p "$home"

exec "$launcher" \
    --res "$res" \
    --screenshot "$shot" \
    --delay 6000 \
    --log-dir "$logdir" \
    --home "$home" \
    --service /tmp/session/0/portal/config="$cfg" \
    --service /tmp/session/0/portal/clipboard="$clip" \
    --service /tmp/session/0/portal/launch="$launch" \
    --golden "$golden" \
    "$ws" "$pixelpaint"
