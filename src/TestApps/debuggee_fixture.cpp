/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// Fixture for the M7 debugger/coredump tests:
// - Debugger: on Serenity a debuggee stops at a loader breakpoint before main;
//   host loaders have no such hook, so this program raises SIGTRAP as its first
//   action to give DebugSession::exec_and_attach a deterministic first stop.
// - CrashReporter: coredump-fixture takes real symbol addresses from this
//   binary to build a synthetic backtrace that resolves against the file on
//   disk.

#include <signal.h>
#include <stdio.h>

__attribute__((noinline)) void inner_crash()
{
    printf("inner_crash reached\n");
}

__attribute__((noinline)) void outer_crash()
{
    inner_crash();
}

int main()
{
    raise(SIGTRAP);
    outer_crash();
    return 0;
}
