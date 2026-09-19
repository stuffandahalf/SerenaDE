#!/bin/sh
# Generic app-golden driver for the batch of apps that render a deterministic
# window with no file argument and no host-dependent content. Launches the app
# against the standard config/clipboard/launch services and compares the
# screenshot to a golden.
#
# A fresh per-run $HOME (--home) keeps window placement deterministic: every
# app restores/saves its size+position from config, so an inherited (or
# accumulated) state would shift the render between runs.
#
# Usage: app-golden.sh <launcher> <res> <screenshot> <logdir> <golden> \
#                      <window-server> <app> <config> <clipboard> <launch>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"; golden="$5"
ws="$6"; app="$7"; cfg="$8"; clip="$9"; launch="${10}"

name=$(basename "$app" | tr '[:upper:]' '[:lower:]')
base="/tmp/serenade-golden-$name"
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
    "$ws" "$app"
