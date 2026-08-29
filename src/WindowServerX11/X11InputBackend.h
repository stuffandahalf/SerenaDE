/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// X11 implementation of InputBackend (M3). The only real input backend; it is
// uniform across every target OS because libX11 behaves identically on Linux
// and the BSDs. evdev was considered and rejected: Linux-only, permission
// model, second maintenance burden. See AGENTS.md, "Architecture decisions".
//
// Event translation table (X event -> SerenaDE structure):
//
//   KeyPress / KeyRelease
//     -> KeyEvent { key        = keysym_to_keycode(XLookupKeysym result),
//                   code_point = from XLookupString,
//                   scancode   = 0 (unused on host),
//                   flags      = X modifier mask -> Mod_* bits | Is_Press,
//                   caps_lock_on = from XQueryKeymap }
//     Key auto-repeat arrives as repeated KeyPress events from the X server;
//     no extra handling needed.
//
//   MotionNotify
//     -> MousePacket { x, y (window coords), is_relative = false }
//     Select ExposureMask | PointerMotionMask | ButtonPressMask |
//     ButtonReleaseMask | EnterWindowMask | LeaveWindowMask.
//
//   ButtonPress / ButtonRelease
//     -> MousePacket button bitmask: X 1/2/3 -> Left/Middle/Right,
//        X 8/9 -> Backward/Forward.
//     Wheel: X delivers it as synthetic button 4 (up) / 5 (down) press+release
//     pairs -> translate to z = +1/-1 with buttons unchanged.
//
// Integration: the X Connection fd is registered with Core::EventLoop via a
// Core::Notifier (Read); start() selects the event mask, stop() deselects.

class X11InputBackend; // defined once LibGfx builds natively (M0)
