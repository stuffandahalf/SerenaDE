#!/bin/sh
# M7 driver: prove the ported Debugger attaches to a live host debugee.
#
# Debugger is a plain CLI app, so no WindowServer is involved. LibDebug's Linux
# backend forks the debugee, traces it via PTRACE_TRACEME and continues until the
# first SIGTRAP (Serenity stops at a loader breakpoint; on hosts the fixture
# raises SIGTRAP itself). With stdin at /dev/null, LibLine hits EOF and the
# session detaches cleanly. The check is functional: the debugger must report its
# first stop and exit 0.
#
# Usage: debugger-attach.sh <debugger> <debugee> <logdir>
set -e

debugger="$1"; debugee="$2"; logdir="$3"

out="$logdir/debugger-attach.log"

# Attach is a fork+ptrace sequence that can race on loaded CI runners, so retry a
# few times. Every attempt's output is printed on failure: an earlier version used
# bare `set -e` with the Debugger output only in the log file, which made a silent
# non-zero exit look like "no log output" in CI.
attempt=1
while [ "$attempt" -le 3 ]; do
    rm -f "$out"
    status=0
    "$debugger" "$debugee" </dev/null >"$out" 2>&1 || status=$?
    if grep -q "Program is stopped at" "$out"; then
        echo "Debugger attached and reported its first stop (attempt $attempt):"
        cat "$out"
        exit 0
    fi
    echo "Debugger attempt $attempt exited with status $status; output was:"
    cat "$out"
    attempt=$((attempt + 1))
done

echo "Debugger did not report a stop after 3 attempts"
exit 1
