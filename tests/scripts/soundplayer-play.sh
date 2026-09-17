#!/bin/sh
# App-completion driver: prove SoundPlayer plays a real audio file on host.
#
# Brings up the real session -- WindowServer + SoundPlayer <file> with
# ConfigServer, Clipboard and LaunchServer as regular services, AudioServer
# (both its sockets) for playback, and ImageDecoder as a broker service (the
# app connects to both at startup and aborts if either is missing) -- using a
# generated 4-second 440Hz sine WAV. While the session runs, parec records
# the default sink's automatic .monitor source, i.e. exactly what PulseAudio
# plays. The assertion is that the capture contains clearly non-silent audio,
# which can only happen if the full path works: SoundPlayer -> LibAudio WAV
# loader -> AudioServer IPC (shared-buffer fd transfer) -> PulseAudio sink.
#
# Skips (exit 77) when the host has no usable PulseAudio, so the test stays
# green on sound-less CI images.
#
# Usage: soundplayer-play.sh <launcher> <res> <screenshot> <logdir> \
#                            <window-server> <soundplayer> <config> <clipboard> \
#                            <launch> <audio> <image>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; soundplayer="$6"; config="$7"; clipboard="$8"; launchsrv="$9"
shift 9
audio="$1"; image="$2"

if ! command -v pactl >/dev/null 2>&1 || ! command -v parec >/dev/null 2>&1; then
    echo "soundplayer-play: PulseAudio tools not found, skipping"
    exit 77
fi
sink=$(pactl info 2>/dev/null | awk -F': *' '/^Default Sink/ {print $2}')
if [ -z "$sink" ] || [ "$sink" = "auto_null" ]; then
    echo "soundplayer-play: no usable default sink, skipping"
    exit 77
fi

base=$(mktemp -d /tmp/serenade-soundplayer-play.XXXXXX)
parec_pid=0
cleanup() {
    [ "$parec_pid" != 0 ] && kill "$parec_pid" 2>/dev/null || true
    rm -rf "$base"
}
trap cleanup EXIT INT TERM

mkdir -p "$base/home"

# Drop state from previous runs so stale logs never masquerade as this run's
# diagnostics (the launcher only truncates the log files it opens itself).
rm -f "$shot" "$logdir/serenade-windowserver.pid" "$logdir"/serenade-*.log

# A 4-second 440Hz stereo sine wave, PCM16, generated with the stdlib.
python3 - "$base/tone.wav" <<'EOF'
import math
import struct
import sys
import wave

rate = 44100
seconds = 4
freq = 440.0
amp = 12000
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(rate)
    frames = bytearray()
    for i in range(rate * seconds):
        v = int(amp * math.sin(2 * math.pi * freq * i / rate))
        frames += struct.pack("<hh", v, v)
    w.writeframes(bytes(frames))
EOF

dump_logs() {
    echo "soundplayer-play: session logs:"
    for f in "$logdir"/serenade-app.log "$logdir"/serenade-service-*.log "$logdir"/serenade-broker-*.log; do
        [ -f "$f" ] || continue
        echo "--- $f ---"
        cat "$f"
    done
}

# Record what the default sink actually plays for the whole session.
parec --device="$sink.monitor" --format=s16le --rate=44100 --channels=2 \
    > "$base/cap.raw" 2>/dev/null &
parec_pid=$!
sleep 1

# The tone starts as soon as the app connects to AudioServer (at startup) and
# lasts 4s; keep the session open long enough to cover startup + playback.
"$launcher" \
    --res "$res" \
    --screenshot "$shot" \
    --delay 8000 \
    --log-dir "$logdir" \
    --home "$base/home" \
    --service /tmp/session/0/portal/config="$config" \
    --service /tmp/session/0/portal/clipboard="$clipboard" \
    --service /tmp/session/0/portal/launch="$launchsrv" \
    --service /tmp/session/0/portal/audio="$audio" \
    --service /tmp/session/0/portal/audiomanager="$audio" \
    --broker-service /tmp/session/0/portal/image="$image" \
    "$ws" "$soundplayer" "$base/tone.wav"

kill "$parec_pid" 2>/dev/null || true
wait "$parec_pid" 2>/dev/null || true
parec_pid=0

# Assert: at least two loud (RMS > 500) seconds in the capture. The tone plays
# for 4s, so timing jitter cannot drop it below this; silence or a dead audio
# path yields zero. A too-short capture means the harness itself did not run
# long enough to be meaningful -> skip rather than fail.
python3 - "$base/cap.raw" <<'EOF'
import math
import struct
import sys

raw = open(sys.argv[1], "rb").read()
count = len(raw) // 2
if count < 44100 * 2 * 6:
    print("soundplayer-play: capture too short (%d samples), skipping" % count)
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
    print("soundplayer-play: no audible playback captured (loud seconds: %d)" % loud)
    sys.exit(1)
print("soundplayer-play: OK, %d loud second(s) of audio captured" % loud)
EOF
