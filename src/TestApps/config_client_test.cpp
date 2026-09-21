/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// config-client-test: a minimal non-GUI ConfigServer client for the
// app-configserver-robustness test. It runs as the launcher's "app" in a
// session whose XDG_CONFIG_HOME points at an uncreatable path, so every
// domain open on the server side fails. It asserts that reads degrade to the
// supplied fallback values and writes are dropped -- then the driver script
// runs a second copy to prove the server is still alive and answering.

#include <AK/StringView.h>
#include <LibConfig/Client.h>
#include <LibCore/EventLoop.h>
#include <cstdio>

int main()
{
    // Synchronous IPC calls schedule message handling through the current
    // EventLoop (Core::deferred_invoke), so one must exist even though this
    // program never runs the loop.
    Core::EventLoop event_loop;

    auto& client = Config::Client::the();

    auto value = client.read_string("RobustnessTest"sv, "Group"sv, "Key"sv, "fallback"sv);
    if (value != "fallback"sv) {
        fprintf(stderr, "config-client-test: read returned '%s', expected 'fallback'\n", value.characters());
        return 1;
    }

    client.write_string("RobustnessTest"sv, "Group"sv, "Key"sv, "written"sv);

    auto value_after_write = client.read_string("RobustnessTest"sv, "Group"sv, "Key"sv, "fallback"sv);
    if (value_after_write != "fallback"sv) {
        fprintf(stderr, "config-client-test: post-write read returned '%s', expected 'fallback'\n", value_after_write.characters());
        return 1;
    }

    auto number = client.read_i32("RobustnessTest"sv, "Group"sv, "Number"sv, -42);
    if (number != -42) {
        fprintf(stderr, "config-client-test: i32 read returned %d, expected -42\n", number);
        return 1;
    }

    // stderr, not stdout: the launcher's app log only captures stderr.
    fprintf(stderr, "CONFIG_ROBUSTNESS_OK\n");
    return 0;
}
