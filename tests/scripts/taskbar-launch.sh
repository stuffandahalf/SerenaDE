#!/bin/sh
# M4 Taskbar launch-from-desktop driver.
#
# Brings up the real Serenity Taskbar and, through a scripted mouse click on the
# Terminal quick-launch dock icon, launches the built Lagom Terminal binary -- the
# literal "open an app from the desktop" path (Taskbar -> AppFile::spawn_with_escalation
# -> Core::Process::spawn -> new window on the same WindowServer).
#
# The desktop's app discovery is pointed at a controlled apps directory ($SERENADE_APP_DIR)
# holding a single Terminal.af whose Executable is the real built binary, so the dock has a
# working Terminal entry (the pinned .af files point at /bin/... which do not exist on a host).
#
# The assertion is functional (--expect-window), not a pixel golden: the launched terminal is
# interactive (shell prompt + rc files) and non-deterministic. With only the taskbar on screen
# the non-background fraction is ~0.035; a launched Terminal window pushes it to ~0.12, so a
# threshold of 0.07 cleanly distinguishes "Terminal actually launched" from "just the taskbar".
#
# Usage: taskbar-launch.sh <launcher> <res> <screenshot> <logdir> \
#                          <window-server> <taskbar> <config> <clipboard> <launch> <terminal>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; taskbar="$6"; cfg="$7"; clip="$8"; launch="${9}"; terminal="${10}"

base=/tmp/serenade-taskbar
apps="$base/apps"
input="$base/input"
script="$base/click.script"

rm -rf "$base"
mkdir -p "$apps" "$input"
# A single app entry whose executable is the real built Terminal binary.
printf '[App]\nName=&Terminal\nExecutable=%s\nCategory=&Utilities\n' "$terminal" > "$apps/Terminal.af"
# Click the Terminal quick-launch dock icon (3rd slot, fixed position). The start button's
# width is font-determined and the font file is identical across hosts, so this is stable.
printf 'delay 500\nmouse click 145 754 left\ndelay 2000\n' > "$script"

export SERENADE_APP_DIR="$apps"
exec "$launcher" \
    --res "$res" \
    --screenshot "$shot" \
    --delay 7000 \
    --log-dir "$logdir" \
    --input-root "$input" \
    --script "$script" \
    --service /tmp/session/0/portal/config="$cfg" \
    --service /tmp/session/0/portal/clipboard="$clip" \
    --service /tmp/session/0/portal/launch="$launch" \
    --expect-window 0.07 \
    "$ws" "$taskbar"
