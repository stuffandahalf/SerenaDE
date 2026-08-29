/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// X11 implementation of WindowServer's ScreenBackend (M3).
//
// Plan:
//   - One top-level X window (fullscreen by default; embedded window mode
//     later), depth matched to the Gfx::Bitmap format the compositor uses.
//   - Backing store via XShm (Xlib + Xext only -- both present on Linux and
//     all target BSDs); fall back to plain XPutImage if SHM is unavailable.
//   - open(): create window, map it, report the head mode setting from the
//     window geometry.
//   - flush_framebuffer_rects(): for each dirty rect, copy pixels from the
//     Screen's framebuffer bitmap into the SHM image and XShmPutImage
//     (DoNotExpose); XFlush once per batch.
//   - set_head_mode_setting() / resize: XResizeWindow + reallocate backing
//     store; the compositor re-renders.
//
// Hook into WindowServer via a tracked patch that adds a new
// ScreenLayout::Screen::Mode (e.g. "x11") instantiating this backend in
// Screen::open_device() -- mirroring the existing Device/Virtual cases.

class X11ScreenBackend; // defined once LibGfx builds natively (M0)
