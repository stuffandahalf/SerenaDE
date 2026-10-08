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
rm -f "$out"

"$debugger" "$debugee" </dev/null >"$out" 2>&1

if grep -q "Program is stopped at" "$out"; then
    echo "Debugger attached and reported its first stop:"
    cat "$out"
    exit 0
fi

echo "Debugger did not report a stop; output was:"
cat "$out"
exit 1
