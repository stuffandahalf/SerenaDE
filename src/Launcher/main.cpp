/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <cstdio>

// serenade-launcher (scaffold -- real implementation lands in M4).
//
// Planned responsibilities:
//   1. Resolve the Serenity resource root (Base/res) and export it via the
//      environment variables LibGfx/LibGUI expect (fonts, icons, GML, themes,
//      keymaps).
//   2. Prepare $HOME and per-user config locations (ConfigServer reads from
//      there).
//   3. Start services in dependency order:
//        SystemServerShim -> WindowServer -> ConfigServer -> Clipboard
//        -> LaunchServer -> Taskbar
//   4. Forward signals; tear children down in reverse order on exit.

int main()
{
    puts("serenade-launcher: scaffold placeholder -- see AGENTS.md (milestone M4)");
    return 0;
}
