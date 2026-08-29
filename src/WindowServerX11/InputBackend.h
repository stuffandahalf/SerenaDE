/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// Platform-neutral input seam for the WindowServer port.
//
// Serenity's WindowServer consumes input through exactly two entry points
// (Userland/Services/WindowServer/EventLoop.cpp):
//
//   ScreenInput::on_receive_keyboard_data(KeyEvent)    // Kernel/API/KeyCode.h
//   ScreenInput::on_receive_mouse_data(MousePacket)    // Kernel/API/MousePacket.h
//
// An InputBackend translates whatever the host platform provides (X11 events,
// a synthetic script for tests, ...) into those two structures and feeds them
// to an InputSink. WindowServer's own device scanning (/dev/input/*) is left
// dormant on the host: no devices register, and all input arrives through the
// active backend.
//
// Portability rule: only the X11 backend may use Xlib; only a future,
// explicitly Linux-only backend may use evdev. Nothing in WindowServer or
// ScreenInput changes per platform.

namespace SerenaDE {

struct KeyEvent; // Kernel/API/KeyCode.h
struct MousePacket; // Kernel/API/MousePacket.h

namespace Core {
class EventLoop;
}

// Receives translated input. Implemented once, against the real ScreenInput.
class InputSink {
public:
    virtual ~InputSink() = default;
    virtual void keyboard_event(KeyEvent const&) = 0;
    virtual void mouse_event(MousePacket const&) = 0;
};

class InputBackend {
public:
    virtual ~InputBackend() = default;

    // Register this backend's source fd(s) with the Core::EventLoop (via a
    // Core::Notifier) so events are drained while WindowServer runs.
    virtual void start(InputSink&, Core::EventLoop&) = 0;
    virtual void stop() = 0;
};

}
