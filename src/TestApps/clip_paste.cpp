/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// clip-paste: a minimal fixture that reads the shared clipboard over the
// Clipboard IPC service and renders its text in a label. Paired with clip-copy
// (a separate process that sets the clipboard) it proves cross-app copy/paste --
// if the round-trip works, this window shows "SERENADE_CLIP_OK"; if not, "(empty)".

#include <AK/ByteString.h>
#include <LibGUI/Application.h>
#include <LibGUI/Clipboard.h>
#include <LibGUI/Label.h>
#include <LibGUI/Window.h>
#include <LibMain/Main.h>

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    auto app = TRY(GUI::Application::create(arguments));

    auto window = GUI::Window::construct();
    window->set_title("Paster");
    window->resize(280, 120);
    window->move_to(440, 140);

    auto main_widget = window->set_main_widget<GUI::Widget>();
    main_widget->set_fill_with_background_color(true);

    auto label = GUI::Label::construct();
    label->set_text_alignment(Gfx::TextAlignment::Center);
    main_widget->add_child(*label);
    label->move_to(0, 50);
    label->resize(280, 20);

    auto refresh = [&label] {
        auto data_and_type = GUI::Clipboard::the().fetch_data_and_type();
        if (data_and_type.data.is_empty()) {
            label->set_text("(empty)"_string);
            return;
        }
        label->set_text(String::from_byte_string(ByteString::copy(data_and_type.data))
                            .release_value_but_fixme_should_propagate_errors());
    };

    // Reflect live changes too, but the initial read is what the golden checks.
    GUI::Clipboard::the().on_change = [&refresh](auto&) { refresh(); };
    refresh();

    window->show();
    return app->exec();
}
