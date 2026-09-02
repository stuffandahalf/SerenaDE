#!/bin/sh
# M4 FileManager golden-test driver.
#
# Builds a fully controlled home + Documents directory with pinned contents and
# mtimes, then launches FileManager on it through serenade-launcher. Fixed /tmp
# paths (not mktemp) are used deliberately: ctest runs serially for this project,
# and a stable path keeps the window title deterministic across hosts so the
# golden is host-independent.
#
# Usage: filemanager-docs.sh <launcher> <res> <screenshot> <logdir> <golden> \
#                            <window-server> <file-manager> <config> <clipboard> <launch>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"; golden="$5"
ws="$6"; fm="$7"; cfg="$8"; clip="$9"; launch="${10}"

base=/tmp/serenade-fm
home="$base/home"
docs="$home/Documents"

rm -rf "$base"
mkdir -p "$home/.config" "$docs/nested"
printf 'hello\n' > "$docs/alpha.txt"
printf 'world\n' > "$docs/beta.txt"
printf 'x' > "$docs/nested/gamma.txt"
# Pin mtimes so any date column is stable across runs and hosts.
touch -d "2026-01-02 03:04:05" \
    "$docs/alpha.txt" "$docs/beta.txt" "$docs/nested" "$docs/nested/gamma.txt" 2>/dev/null || true

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
    "$ws" "$fm" "$docs"
