#!/bin/sh
# M6 audio driver: prove that audio actually plays in a game (Piano).
#
# Brings up the real session -- WindowServer + Piano with ConfigServer,
# Clipboard and AudioServer (both its sockets) as services -- and scripts a
# mouse press-and-hold on a white key of the on-screen keyboard. While the
# session runs, parec records the default sink's automatic .monitor source,
# i.e. exactly what PulseAudio plays. The assertion is that the capture
# contains clearly non-silent audio (the held note), which can only happen if
# the full path works: key click -> Piano DSP -> AudioServer IPC (including
# the shared-buffer fd transfer) -> PulseAudio sink.
#
# Skips (exit 77) when the host has no usable PulseAudio, so the test stays
# green on sound-less CI images.
#
# Usage: piano-audio.sh <launcher> <res> <screenshot> <logdir> \
#                        <window-server> <piano> <config> <clipboard> <audio>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; piano="$6"; config="$7"; clipboard="$8"; audio="${9}"

if ! command -v pactl >/dev/null 2>&1 || ! command -v parec >/dev/null 2>&1; then
    echo "piano-audio: PulseAudio tools not found, skipping"
    exit 77
fi
sink=$(pactl info 2>/dev/null | awk -F': *' '/^Default Sink/ {print $2}')
if [ -z "$sink" ] || [ "$sink" = "auto_null" ]; then
    echo "piano-audio: no usable default sink, skipping"
    exit 77
fi

base=$(mktemp -d /tmp/serenade-piano-audio.XXXXXX)
parec_pid=0
cleanup() {
    [ "$parec_pid" != 0 ] && kill "$parec_pid" 2>/dev/null || true
    rm -rf "$base"
}
trap cleanup EXIT INT TERM

mkdir -p "$base/home" "$base/input"

# The Piano window (840x600) opens at a fixed position in the virtual screen
# backend: client area rows ~74-692, cols ~105-470. White keys fill the band
# y=632-692; (167,660) is well inside one white key, clear of the black keys
# (which end at y~628). Hold it for 3s so plenty of samples flow.
cat > "$base/script.txt" <<'EOF'
delay 3000
mouse press 167 660
delay 3000
mouse release 167 660
delay 1500
EOF

# Record what the default sink actually plays for the whole session.
parec --device="$sink.monitor" --format=s16le --rate=44100 --channels=2 \
    > "$base/cap.raw" 2>/dev/null &
parec_pid=$!
sleep 1

"$launcher" \
    --res "$res" \
    --screenshot "$shot" \
    --delay 2000 \
    --log-dir "$logdir" \
    --home "$base/home" \
    --input-root "$base/input" \
    --script "$base/script.txt" \
    --service /tmp/session/0/portal/config="$config" \
    --service /tmp/session/0/portal/clipboard="$clipboard" \
    --service /tmp/session/0/portal/audio="$audio" \
    --service /tmp/session/0/portal/audiomanager="$audio" \
    "$ws" "$piano"

kill "$parec_pid" 2>/dev/null || true
wait "$parec_pid" 2>/dev/null || true
parec_pid=0

# Assert: at least two loud (RMS > 500) seconds in the capture. The note is
# held for 3s, so timing jitter cannot drop it below this; silence or a dead
# audio path yields zero.
python3 - "$base/cap.raw" <<'EOF'
import math
import struct
import sys

raw = open(sys.argv[1], "rb").read()
count = len(raw) // 2
if count < 44100 * 2 * 8:
    print("piano-audio: capture too short (%d samples), skipping" % count)
    sys.exit(77)

samples = struct.unpack("<%dh" % count, raw[: count * 2])
window = 44100 * 2  # one second of stereo s16le
loud = 0
for i in range(count // window):
    seg = samples[i * window:(i + 1) * window]
    rms = math.sqrt(sum(s * s for s in seg) / len(seg))
    if rms > 500:
        loud += 1

if loud < 2:
    print("piano-audio: no audible playback captured (loud seconds: %d)" % loud)
    sys.exit(1)
print("piano-audio: OK, %d loud second(s) of audio captured" % loud)
EOF
