/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// Input mapper (M3). This translation unit is the ONLY place in the input path that
// includes Serenity headers; it turns neutral RawX11Event records into ::KeyEvent /
// MousePacket and delivers them to WindowServer's ScreenInput. It must not include
// Xlib (see X11InputBackend.h: the KeyCode name collides).

#include "X11InputBackend.h"

#include <Kernel/API/KeyCode.h>
#include <Kernel/API/MousePacket.h>
#include <WindowServer/Screen.h>

namespace SerenaDE {

namespace {

// Mirrors WindowServer::MouseButton (kept local to avoid a hard event-header dep).
enum {
    MB_Primary = 1,
    MB_Secondary = 2,
    MB_Middle = 4,
    MB_Backward = 8,
    MB_Forward = 16,
};

// Standard X modifier bit positions (Xlib not included here).
enum {
    X_ShiftMask = 1 << 0,
    X_ControlMask = 1 << 2,
    X_Mod1Mask = 1 << 3, // Alt
    X_Mod4Mask = 1 << 6, // Super/Win
};

unsigned modifiers_from_state(unsigned state)
{
    unsigned mods = 0;
    if (state & X_ShiftMask)
        mods |= Mod_Shift;
    if (state & X_ControlMask)
        mods |= Mod_Ctrl;
    if (state & X_Mod1Mask)
        mods |= Mod_Alt;
    if (state & X_Mod4Mask)
        mods |= Mod_Super;
    return mods;
}

KeyCode key_code_for(RawX11Event const& re)
{
    if (re.key == RawKey::Char)
        return code_point_to_key_code(re.key_char);
    switch (re.key) {
    case RawKey::Escape: return Key_Escape;
    case RawKey::Tab: return Key_Tab;
    case RawKey::Return: return Key_Return;
    case RawKey::Backspace: return Key_Backspace;
    case RawKey::Insert: return Key_Insert;
    case RawKey::Delete: return Key_Delete;
    case RawKey::Home: return Key_Home;
    case RawKey::End: return Key_End;
    case RawKey::Left: return Key_Left;
    case RawKey::Up: return Key_Up;
    case RawKey::Right: return Key_Right;
    case RawKey::Down: return Key_Down;
    case RawKey::PageUp: return Key_PageUp;
    case RawKey::PageDown: return Key_PageDown;
    case RawKey::F1: return Key_F1;
    case RawKey::F2: return Key_F2;
    case RawKey::F3: return Key_F3;
    case RawKey::F4: return Key_F4;
    case RawKey::F5: return Key_F5;
    case RawKey::F6: return Key_F6;
    case RawKey::F7: return Key_F7;
    case RawKey::F8: return Key_F8;
    case RawKey::F9: return Key_F9;
    case RawKey::F10: return Key_F10;
    case RawKey::F11: return Key_F11;
    case RawKey::F12: return Key_F12;
    case RawKey::LeftShift: return Key_LeftShift;
    case RawKey::RightShift: return Key_RightShift;
    case RawKey::LeftControl: return Key_LeftControl;
    case RawKey::RightControl: return Key_RightControl;
    case RawKey::LeftAlt: return Key_LeftAlt;
    case RawKey::RightAlt: return Key_RightAlt;
    case RawKey::CapsLock: return Key_CapsLock;
    default: return Key_Invalid;
    }
}

// Mouse state shared across events (absolute position + pressed buttons).
unsigned s_buttons = 0;
int s_last_x = 0;
int s_last_y = 0;
int s_width = 0;
int s_height = 0;

void deliver_mouse(int z)
{
    if (s_width <= 0 || s_height <= 0)
        return;
    MousePacket packet {};
    packet.x = s_last_x * 0xffff / s_width;
    packet.y = s_last_y * 0xffff / s_height;
    packet.z = z;
    packet.w = 0;
    packet.buttons = static_cast<u8>(s_buttons);
    packet.is_relative = false;
    WindowServer::ScreenInput::the().on_receive_mouse_data(packet);
}

void on_raw(RawX11Event const& re)
{
    switch (re.type) {
    case RawX11Type::Motion:
        s_last_x = re.x;
        s_last_y = re.y;
        deliver_mouse(0);
        break;

    case RawX11Type::ButtonDown:
    case RawX11Type::ButtonUp: {
        bool press = (re.type == RawX11Type::ButtonDown);
        int b = static_cast<int>(re.button);
        s_last_x = re.x;
        s_last_y = re.y;

        if (b == 4 || b == 5) { // wheel: synthetic button 4 (up) / 5 (down)
            deliver_mouse(press ? (b == 4 ? 1 : -1) : 0);
            break;
        }

        unsigned bit = 0;
        switch (b) {
        case 1: bit = MB_Primary; break;
        case 2: bit = MB_Middle; break;
        case 3: bit = MB_Secondary; break;
        case 8: bit = MB_Backward; break;
        case 9: bit = MB_Forward; break;
        default: break;
        }
        if (bit) {
            if (press)
                s_buttons |= bit;
            else
                s_buttons &= ~bit;
            deliver_mouse(0);
        }
        break;
    }

    case RawX11Type::KeyDown:
    case RawX11Type::KeyUp: {
        bool press = (re.type == RawX11Type::KeyDown);
        u32 code_point = (re.key == RawKey::Char) ? re.key_char : 0;
        ::KeyEvent ke {};
        ke.key = key_code_for(re);
        ke.code_point = code_point;
        ke.scancode = 0;
        ke.map_entry_index = 0;
        ke.flags = static_cast<u8>(modifiers_from_state(re.state) | (press ? Is_Press : 0));
        WindowServer::ScreenInput::the().on_receive_keyboard_data(ke);
        break;
    }

    default:
        break;
    }
}

}

void x11_deliver_raw(RawX11Event const& re)
{
    on_raw(re);
}

void x11_set_viewport(int width, int height)
{
    s_width = width;
    s_height = height;
}

}
