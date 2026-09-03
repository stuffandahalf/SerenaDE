/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// launch-terminal: a minimal stand-in for the desktop's "open this app" action.
// It asks LaunchServer to open the executable named in argv[1] -- exactly what the
// desktop menu does when you pick an app (Desktop::Launcher -> LaunchServer IPC ->
// Core::Process::spawn). Used by the M4 "launch Terminal from the desktop" test:
// the launcher runs this as the primary app, and a separately-spawned Terminal
// window must appear as a result.

#include <AK/ByteString.h>
#include <LibCore/EventLoop.h>
#include <LibDesktop/Launcher.h>
#include <cstring>
#include <LibMain/Main.h>
#include <LibURL/URL.h>

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    // Desktop::Launcher's synchronous IPC uses Core::deferred_invoke for its
    // response, which requires a current EventLoop on this thread.
    Core::EventLoop loop;

    if (arguments.argc < 2) {
        warnln("usage: launch-terminal <executable-path>");
        return 1;
    }
    auto path = ByteString(StringView(arguments.argv[1], strlen(arguments.argv[1])));

    auto url = URL::create_with_url_or_path(path);

    // Synchronous IPC: blocks until LaunchServer has handled the request (which
    // spawns the app), then returns whether it succeeded.
    bool ok = Desktop::Launcher::open(url);
    dbgln("launch-terminal: LaunchServer open('{}') -> {}", path, ok ? "ok" : "failed");
    return ok ? 0 : 1;
}
