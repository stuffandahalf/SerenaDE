/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// resize-test-window: a minimal Serenity GUI application used by the M2
// golden-screenshot tests to exercise programmatic input (click, drag-to-move,
// drag-to-resize). It is deliberately simple and deterministic: one normal,
// resizable window at a fixed position with a label and a click-counting button.

#include <LibGUI/Application.h>
#include <LibGUI/Button.h>
#include <LibGUI/Label.h>
#include <LibGUI/Window.h>
#include <LibMain/Main.h>

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    auto app = TRY(GUI::Application::create(arguments));

    auto window = GUI::Window::construct();
    window->set_title("Serenade Input Test");
    window->resize(300, 200);
    // Normal window type and resizable (both the defaults): the M2 input tests
    // drag its titlebar to move it and its bottom-right corner to resize it.
    window->move_to(100, 80);

    auto main_widget = window->set_main_widget<GUI::Widget>();
    main_widget->set_fill_with_background_color(true);

    auto label = GUI::Label::construct();
    label->set_text_alignment(Gfx::TextAlignment::Center);
    main_widget->add_child(*label);
    label->move_to(0, 40);
    label->resize(300, 20);

    auto button = GUI::Button::construct();
    button->set_text("Clicked 0 times"_string);
    main_widget->add_child(*button);
    button->move_to(105, 90);
    button->resize(90, 28);

    int clicks = 0;
    button->on_click = [&label, &clicks](auto) {
        ++clicks;
        label->set_text(String::formatted("Clicked {} time{}", clicks, clicks == 1 ? "" : "s")
                            .release_value_but_fixme_should_propagate_errors());
    };

    window->show();

    // Report the client-side geometry (screen coordinates) so the input-test
    // scripts can target the titlebar and resize corner precisely.
    {
        auto r = window->rect();
        fprintf(stderr, "RESIZE_TEST_CONTENT_RECT %d %d %d %d\n", r.x(), r.y(), r.width(), r.height());
        auto p = window->position();
        fprintf(stderr, "RESIZE_TEST_POSITION %d %d\n", p.x(), p.y());
    }

    return app->exec();
}
