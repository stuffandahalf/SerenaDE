/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// Neutral bridge between the X11 world and the Serenity world for input (M3).
//
// Xlib.h defines a `KeyCode` typedef that collides with Serenity's `enum KeyCode`,
// so no translation unit may include both. The X event reader lives in
// X11Context.cpp (Xlib only) and turns raw X events into these neutral records; the
// mapper (X11InputMap.cpp, Serenity only) turns them into ::KeyEvent / MousePacket
// and delivers them to WindowServer's ScreenInput via x11_deliver_raw(). This header
// is includable from both sides because it references neither Xlib nor Serenity types.

#include <cstddef>

namespace SerenaDE {

// NOTE: enumerator names deliberately avoid Xlib macro names (ButtonPress, KeyPress,
// ...) which are #defined by Xlib.h and would expand here.
enum class RawX11Type : unsigned char {
    Motion,
    ButtonDown,
    ButtonUp,
    KeyDown,
    KeyUp,
};

// A key, reduced to what the mapper needs (no Xlib keysym types leak across).
// NOTE: enumerator names deliberately avoid Xlib macro names (e.g. `None`, defined as
// 0L in X.h) which would expand here.
enum class RawKey : unsigned {
    NoKey,
    Escape, Tab, Return, Backspace, Insert, Delete, Home, End,
    Left, Up, Right, Down, PageUp, PageDown,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt, CapsLock,
    Char, // printable character; see key_char
};

struct RawX11Event {
    RawX11Type type {};
    int x { 0 };
    int y { 0 };
    unsigned button { 0 }; // X button number (mouse)
    RawKey key { RawKey::NoKey }; // keyboard
    unsigned char state { 0 }; // X modifier state (keyboard)
    unsigned char key_char { 0 }; // printable char when key == Char
};

// Deliver one already-translated input event to WindowServer's ScreenInput.
// Implemented in the Serenity-only mapper (X11InputMap.cpp); no Xlib here. Must run
// on the WindowServer's main thread.
void x11_deliver_raw(RawX11Event const& re);

// Tell the mapper the current screen size so it can normalize absolute mouse
// coordinates into WindowServer's 0..0xffff space. Called once at setup.
void x11_set_viewport(int width, int height);

}
