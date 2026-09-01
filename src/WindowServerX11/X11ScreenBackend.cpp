/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "X11ScreenBackend.h"

#include "X11Context.h"
#include <AK/OwnPtr.h>
#include <LibGfx/Color.h>

namespace WindowServer {

// Defined here (in the SerenaDE X11 library) and declared in the Serenity tree
// by the tracked patch; keeps all Xlib usage out of WindowServer's own sources.
NonnullOwnPtr<ScreenBackend> serenade_x11_create_screen_backend()
{
    return make<X11ScreenBackend>();
}

X11ScreenBackend::~X11ScreenBackend() = default;

ErrorOr<void> X11ScreenBackend::open()
{
    // Single-buffered: the compositor copies changed rects into our front buffer
    // (the shared XShm store) before flushing, so we only ever push final pixels.
    m_can_device_flush_buffers = true;
    m_can_device_flush_entire_framebuffer = true;
    m_can_set_head_buffer = false;

    // Open $DISPLAY now so a missing display fails here (cleanly) rather than
    // tripping a MUST inside Screen::set_resolution later.
    auto result = SerenaDE::X11Context::the().open_display();
    if (result.is_error())
        return result;

    // Register the X connection with the WindowServer's event loop so Expose (and,
    // later, input) events are drained while it runs. The loop is current here.
    SerenaDE::X11Context::the().start_event_pump();
    return {};
}

void X11ScreenBackend::set_head_buffer(int)
{
    // Single buffer; nothing to flip.
}

ErrorOr<void> X11ScreenBackend::set_safe_head_mode_setting()
{
    return {};
}

ErrorOr<void> X11ScreenBackend::set_head_mode_setting(GraphicsHeadModeSetting mode_setting)
{
    m_height = mode_setting.vertical_active;

    if (mode_setting.horizontal_stride == 0)
        mode_setting.horizontal_stride = static_cast<int>(mode_setting.horizontal_active * sizeof(Gfx::ARGB32));
    m_pitch = mode_setting.horizontal_stride;
    if (static_cast<int>(mode_setting.horizontal_active * sizeof(Gfx::ARGB32)) != mode_setting.horizontal_stride)
        return Error::from_string_literal("Unsupported pitch");

    m_width = mode_setting.horizontal_active;
    return {};
}

ErrorOr<GraphicsHeadModeSetting> X11ScreenBackend::get_head_mode_setting()
{
    return GraphicsHeadModeSetting {
        .horizontal_stride = m_pitch,
        .pixel_clock_in_khz = 0,
        .horizontal_active = m_width,
        .horizontal_front_porch_pixels = 0,
        .horizontal_sync_time_pixels = 0,
        .horizontal_blank_pixels = 0,
        .vertical_active = m_height,
        .vertical_front_porch_lines = 0,
        .vertical_sync_time_lines = 0,
        .vertical_blank_lines = 0,
        .horizontal_offset = 0,
        .vertical_offset = 0,
    };
}

ErrorOr<void> X11ScreenBackend::map_framebuffer()
{
    // The resolution is known by now (set_head_mode_setting ran first). Create
    // the window + shared store and point our framebuffer at it.
    TRY(SerenaDE::X11Context::the().create_window(m_width, m_height));

    m_framebuffer = reinterpret_cast<Gfx::ARGB32*>(SerenaDE::X11Context::the().pixels());
    m_size_in_bytes = static_cast<size_t>(m_pitch) * m_height;
    m_max_size_in_bytes = m_size_in_bytes;
    m_back_buffer_offset = 0; // single buffer

    return {};
}

ErrorOr<void> X11ScreenBackend::unmap_framebuffer()
{
    // The shared context (and its window/store) outlives the backend for now;
    // create_window() is a no-op if the window already exists.
    m_framebuffer = nullptr;
    return {};
}

ErrorOr<void> X11ScreenBackend::flush_framebuffer_rects(int, ReadonlySpan<FBRect> rects)
{
    auto& context = SerenaDE::X11Context::the();
    for (auto const& rect : rects)
        context.put_rect(static_cast<int>(rect.x), static_cast<int>(rect.y), rect.width, rect.height);
    context.flush();
    return {};
}

ErrorOr<void> X11ScreenBackend::flush_framebuffer()
{
    auto& context = SerenaDE::X11Context::the();
    context.put_rect(0, 0, m_width, m_height);
    context.flush();
    return {};
}

}
