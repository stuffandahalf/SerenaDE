#!/bin/sh
# M4 FileManager launch/browse driver.
#
# Builds a fully controlled home + Documents directory with known contents and
# pinned mtimes, then launches FileManager on it through serenade-launcher. Fixed
# /tmp paths (not mktemp) are used deliberately: ctest runs serially for this
# project, so a stable path is safe and keeps the test reproducible.
#
# The assertion is functional (--expect-window), not a pixel golden: FileManager's
# full window includes host-dependent content (the sidebar's system-location
# favorites, date-column timezone) that renders differently across hosts, so an
# exact golden is not portable. We instead assert that a substantial directory
# window actually appeared over the real (controlled) filesystem.
#
# Usage: filemanager-docs.sh <launcher> <res> <screenshot> <logdir> \
#                            <window-server> <file-manager> <config> <clipboard> <launch>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; fm="$6"; cfg="$7"; clip="$8"; launch="${9}"

base=/tmp/serenade-fm
home="$base/home"
docs="$home/Documents"

rm -rf "$base"
mkdir -p "$home/.config" "$docs/nested"
printf 'hello\n' > "$docs/alpha.txt"
printf 'world\n' > "$docs/beta.txt"
printf 'x' > "$docs/nested/gamma.txt"
# Pin mtimes so the view is stable across runs.
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
    --expect-window 0.10 \
    "$ws" "$fm" "$docs"
