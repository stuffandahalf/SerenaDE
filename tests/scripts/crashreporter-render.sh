#!/bin/sh
# M7 driver: prove CrashReporter renders a coredump on host.
#
# coredump-fixture writes a synthetic Serenity-format ET_CORE file whose thread
# registers and stack frame chain point at real symbols in the debuggee fixture
# binary, so LibCoredump's backtrace resolves against an actual ELF on disk.
# CrashReporter then opens its window over that file; the check is functional
# (--expect-window plus the backtrace lines in the app log). No pixel golden:
# symbol addresses are build-specific.
#
# Usage: crashreporter-render.sh <launcher> <res> <screenshot> <logdir> \
#                                <window-server> <crash-reporter> \
#                                <config> <clipboard> <launch> \
#                                <coredump-fixture> <debuggee>
set -e

launcher="$1"; res="$2"; shot="$3"; logdir="$4"
ws="$5"; cr="$6"; cfg="$7"; clip="$8"; launchsrv="$9"
fixture="${10}"; debugee="${11}"

base=$(mktemp -d /tmp/serenade-crashreporter-render.XXXXXX)
cleanup() {
    rm -rf "$base"
}
trap cleanup EXIT INT TERM

mkdir -p "$base/home/.config" "$base/home/.local/share" "$base/runtime"

# Pin the XDG dirs to the session home, as the other GUI drivers do.
export XDG_RUNTIME_DIR="$base/runtime"
export XDG_CONFIG_HOME="$base/home/.config"
export XDG_DATA_HOME="$base/home/.local/share"

rm -f "$shot" "$logdir/serenade-windowserver.pid" "$logdir"/serenade-*.log

core="$base/crashy.core"
"$fixture" "$core" "$debugee"

"$launcher" \
    --res "$res" \
    --screenshot "$shot" \
    --delay 6000 \
    --log-dir "$logdir" \
    --home "$base/home" \
    --service /tmp/session/0/portal/config="$cfg" \
    --service /tmp/session/0/portal/clipboard="$clip" \
    --service /tmp/session/0/portal/launch="$launchsrv" \
    --expect-window 0.05 \
    "$ws" "$cr" "$core"

if grep -q "Backtrace for thread #0 (TID 1)" "$logdir/serenade-app.log" && grep -q "inner_crash()" "$logdir/serenade-app.log"; then
    echo "CrashReporter rendered the synthetic coredump with a resolved backtrace:"
    grep -A3 "Backtrace for thread" "$logdir/serenade-app.log"
    exit 0
fi

echo "CrashReporter did not emit a backtrace; app log was:"
cat "$logdir/serenade-app.log"
exit 1
