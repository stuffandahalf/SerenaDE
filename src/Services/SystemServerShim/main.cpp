/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// SystemServerShim (M4 -- scaffold placeholder, not yet built).
//
// Purpose: Serenity apps launch other processes through the SystemServer IPC
// contract. On the host there is no kernel process manager, so this shim binds
// the same IPC endpoint and implements the contract with fork/exec of real
// host binaries (resolved against the SerenaDE app directory), plus
// child-exit tracking for the Taskbar's process list.
//
// Design constraint: apps must not be patched to call something else -- the
// shim exists precisely to keep the Serenity tree unmodified here.

int main()
{
    // TODO(M4): bind SystemServer IPC endpoint, implement spawn/kill/exit tracking.
    return 1;
}
