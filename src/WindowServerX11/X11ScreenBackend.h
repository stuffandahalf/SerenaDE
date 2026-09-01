/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// X11 implementation of WindowServer's ScreenBackend (M3).
//
// The compositor paints directly into m_framebuffer, which we point at the shared
// XShm store (see SerenaDE::X11Context); presenting a frame is then just blitting
// the dirty rects out of that store with XShmPutImage. Single-buffered
// (m_can_set_head_buffer = false): the compositor renders to its own back bitmap
// and copies changed rects into our front buffer before calling us, so we only
// ever push already-final pixels to X.
//
// Selected by Screen::open_device() when the screen mode is "X11" (see the
// tracked patch that adds the mode + this case). The factory below keeps all Xlib
// usage out of the Serenity tree: WindowServer only calls these two hooks.

#include <WindowServer/ScreenBackend.h>

namespace WindowServer {

class X11ScreenBackend : public ScreenBackend {
public:
    ~X11ScreenBackend() override;

    // Hook called from Screen::open_device() for Mode::X11. Returns a backend
    // whose open() opens $DISPLAY (failing cleanly if it is unset).
    ErrorOr<void> open() override;
    void set_head_buffer(int) override;
    ErrorOr<void> flush_framebuffer_rects(int, ReadonlySpan<FBRect>) override;
    ErrorOr<void> unmap_framebuffer() override;
    ErrorOr<void> map_framebuffer() override;
    ErrorOr<void> flush_framebuffer() override;
    ErrorOr<void> set_safe_head_mode_setting() override;
    ErrorOr<void> set_head_mode_setting(GraphicsHeadModeSetting) override;
    ErrorOr<GraphicsHeadModeSetting> get_head_mode_setting() override;

private:
    int m_width { 0 };
    int m_height { 0 };
};

}
