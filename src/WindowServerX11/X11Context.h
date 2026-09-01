/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// Shared X11 display context for the WindowServer port (M3).
//
// Both the X11ScreenBackend (display) and the X11InputBackend (real mouse/keyboard)
// operate on the same top-level window of one X connection, so they share this
// singleton. It owns:
//   - the Display* (one X connection to $DISPLAY),
//   - a top-level window (the "screen") in a TrueColor visual,
//   - the pixel store that is presented to X.
//
// The compositor always paints Gfx::ARGB32 (4 bytes/pixel) into pixels(). How that
// reaches X depends on the server's best visual:
//   - Zero-copy: a 32-bit TrueColor visual whose masks match ARGB32 byte order is
//     used; the shared store IS the framebuffer and we blit it directly.
//   - Conversion: otherwise (e.g. a 24-bit server) we keep a separate ARGB32 buffer
//     for the compositor and convert each dirty rect into the native format on flush.
//
// Two-phase setup mirrors Screen's own sequencing: open_display() runs in the
// backend's open() (so a missing $DISPLAY fails early and cleanly), and
// create_window() runs in map_framebuffer() once the resolution is known.
//
// Only this file and the two backends may touch Xlib; nothing in WindowServer or
// ScreenInput is X11-specific.

#include <AK/Atomic.h>
#include <AK/Forward.h>
#include <AK/RefPtr.h>
#include <LibCore/Notifier.h>
#include <X11/Xlib.h>

#include <thread>

namespace SerenaDE {

class X11Context {
public:
    static X11Context& the();

    // Open $DISPLAY. Idempotent. Fails (no DISPLAY) rather than aborting so the
    // caller can report a clean error instead of tripping a MUST later.
    AK::ErrorOr<void> open_display();

    // Create and map a fullscreen TrueColor window of the given size and set up the
    // pixel store (XShm when available). Requires open_display() to have succeeded.
    AK::ErrorOr<void> create_window(int width, int height);

    bool is_initialized() const { return m_display != nullptr; }
    bool has_window() const { return m_window != 0; }

    Display* display() const { return m_display; }
    Window window() const { return m_window; }
    GC gc() const { return m_gc; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    // The ARGB32 framebuffer the compositor paints into (always 4 bytes/pixel).
    unsigned char* pixels() const { return m_fb; }

    // Blit one already-updated rect out to the window, then flush once. Coords are
    // in framebuffer (pixel) space, 1:1 with the window. Converts format if needed.
    void put_rect(int x, int y, unsigned width, unsigned height);
    void flush();

    // Start draining X events on a repeating timer in the current Core::EventLoop.
    // Must be called while the server's event loop is current (i.e. from the
    // backend's open()). Without periodic draining the X server back-pressures and
    // our display requests are never processed, leaving the window cleared.
    void start_event_pump();

private:
    X11Context() = default;

    // Drain pending X events: deliver input events to ScreenInput (via the mapper)
    // and re-blit any exposed region from the framebuffer. Never discards input.
    void pump_events();

    // Push one rect of the pixel store out to the window (convert + XPutImage). No
    // event handling, no flush; the caller flushes. Used by put_rect() and for Expose.
    void blit(int x, int y, unsigned width, unsigned height);

    // Convert one rect from the ARGB32 framebuffer into the native image store.
    void convert_rect(int x, int y, unsigned width, unsigned height);

    // Pump-thread body: select() on the X connection and nudge the main thread via
    // the self-pipe when there is data (or periodically). No Xlib or ScreenInput use.
    void pump_thread_main();

    AK::RefPtr<Core::Notifier> m_pump_notifier {};
    int m_wake_pipe[2] { -1, -1 };
    AK::Atomic<bool> m_pumping { false };

    Display* m_display { nullptr };
    Window m_window { 0 };
    GC m_gc { 0 };
    Visual* m_visual { nullptr };
    int m_depth { 24 };
    int m_width { 0 };
    int m_height { 0 };

    unsigned char* m_fb { nullptr };        // ARGB32, width*height*4 (compositor target)
    unsigned char* m_image_data { nullptr }; // native format, the X backing store
    XImage* m_image { nullptr };
    bool m_zero_copy { false };
    bool m_use_shm { false };
};

}
