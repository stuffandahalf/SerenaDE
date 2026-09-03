/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// clip-copy: a minimal fixture that publishes a fixed string to the shared
// clipboard over the Clipboard IPC service. Paired with clip-paste (a separate
// process) it proves text copied by one app is visible to another -- the M4
// "copy/paste between two apps" exit criterion.

#include <LibGUI/Application.h>
#include <LibGUI/Clipboard.h>
#include <LibGUI/Label.h>
#include <LibGUI/Window.h>
#include <LibMain/Main.h>

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    auto app = TRY(GUI::Application::create(arguments));

    auto window = GUI::Window::construct();
    window->set_title("Copier");
    window->resize(240, 120);
    window->move_to(120, 140);

    auto main_widget = window->set_main_widget<GUI::Widget>();
    main_widget->set_fill_with_background_color(true);

    auto label = GUI::Label::construct();
    label->set_text("copied to clipboard"_string);
    label->set_text_alignment(Gfx::TextAlignment::Center);
    main_widget->add_child(*label);
    label->move_to(0, 50);
    label->resize(240, 20);

    // Publish the fixed string through the Clipboard service. clip-paste (a
    // separate process) will fetch it and render it.
    GUI::Clipboard::the().set_plain_text("SERENADE_CLIP_OK"sv);

    window->show();
    return app->exec();
}
