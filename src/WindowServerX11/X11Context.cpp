/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "X11Context.h"

#include "X11InputBackend.h"

#include <AK/Error.h>
#include <LibCore/EventLoop.h>
#include <LibCore/Notifier.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysymdef.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace SerenaDE {

namespace {

// Reduce an X key event to the neutral RawKey the mapper needs. Xlib only (this TU
// must not include Serenity's KeyCode header, which collides with Xlib's typedef).
RawKey raw_key_for(XKeyEvent& ev, unsigned char& out_char)
{
    out_char = 0;
    char buf[8] = {};
    KeySym unused = 0;
    int n = XLookupString(&ev, buf, sizeof(buf), &unused, nullptr);
    if (n > 0 && (unsigned char)buf[0] >= 32 && (unsigned char)buf[0] < 127) {
        out_char = static_cast<unsigned char>(buf[0]);
        return RawKey::Char;
    }
    switch (XLookupKeysym(&ev, 0)) {
    case XK_Escape: return RawKey::Escape;
    case XK_Tab: return RawKey::Tab;
    case XK_Return:
    case XK_KP_Enter: return RawKey::Return;
    case XK_BackSpace: return RawKey::Backspace;
    case XK_Insert: return RawKey::Insert;
    case XK_Delete: return RawKey::Delete;
    case XK_Home: return RawKey::Home;
    case XK_End: return RawKey::End;
    case XK_Left: return RawKey::Left;
    case XK_Up: return RawKey::Up;
    case XK_Right: return RawKey::Right;
    case XK_Down: return RawKey::Down;
    case XK_Page_Up: return RawKey::PageUp;
    case XK_Page_Down: return RawKey::PageDown;
    case XK_F1: return RawKey::F1;
    case XK_F2: return RawKey::F2;
    case XK_F3: return RawKey::F3;
    case XK_F4: return RawKey::F4;
    case XK_F5: return RawKey::F5;
    case XK_F6: return RawKey::F6;
    case XK_F7: return RawKey::F7;
    case XK_F8: return RawKey::F8;
    case XK_F9: return RawKey::F9;
    case XK_F10: return RawKey::F10;
    case XK_F11: return RawKey::F11;
    case XK_F12: return RawKey::F12;
    case XK_Shift_L: return RawKey::LeftShift;
    case XK_Shift_R: return RawKey::RightShift;
    case XK_Control_L: return RawKey::LeftControl;
    case XK_Control_R: return RawKey::RightControl;
    case XK_Alt_L: return RawKey::LeftAlt;
    case XK_Alt_R: return RawKey::RightAlt;
    case XK_Caps_Lock: return RawKey::CapsLock;
    default: return RawKey::NoKey;
    }
}

}

X11Context& X11Context::the()
{
    static X11Context s_context;
    return s_context;
}

AK::ErrorOr<void> X11Context::open_display()
{
    if (m_display != nullptr)
        return {};
    m_display = XOpenDisplay(nullptr);
    if (m_display == nullptr)
        return Error::from_string_literal("X11Context: cannot open $DISPLAY");
    return {};
}

AK::ErrorOr<void> X11Context::create_window(int width, int height)
{
    if (m_display == nullptr)
        return Error::from_string_literal("X11Context::create_window before open_display");
    if (m_window != 0)
        return {}; // already created (fixed resolution for now)

    m_width = width;
    m_height = height;

    int screen = DefaultScreen(m_display);
    Window root = RootWindow(m_display, screen);

    // Use the screen's default visual/depth so the window matches what the server
    // actually scans out. Forcing a deeper visual (e.g. 32-bit on a 24-bit screen)
    // yields a window that is created but never displays. We still take the fast
    // zero-copy path when the default happens to be a matching 32-bit TrueColor
    // whose masks line up with Gfx::ARGB32 (little-endian B,G,R,x); otherwise we
    // convert ARGB32 into the native format on each flush.
    m_depth = DefaultDepth(m_display, screen);
    m_visual = DefaultVisual(m_display, screen);

    unsigned red_mask = 0xFF0000, green_mask = 0x00FF00, blue_mask = 0x0000FF;
    XVisualInfo vinfo {};
    if (XMatchVisualInfo(m_display, screen, m_depth, TrueColor, &vinfo)) {
        red_mask = vinfo.red_mask;
        green_mask = vinfo.green_mask;
        blue_mask = vinfo.blue_mask;
    }
    m_zero_copy = (m_depth == 32) && (red_mask == 0xFF0000) && (green_mask == 0x00FF00) && (blue_mask == 0x0000FF);

    Colormap colormap = XCreateColormap(m_display, root, m_visual, AllocNone);

    XSetWindowAttributes attrs {};
    attrs.colormap = colormap;
    attrs.background_pixel = BlackPixel(m_display, screen);
    attrs.border_pixel = WhitePixel(m_display, screen);
    // Select the full input mask now: the input backend reads from this same
    // window, so both display and input share one event source.
    attrs.event_mask = ExposureMask | PointerMotionMask | ButtonPressMask
        | ButtonReleaseMask | KeyPressMask | KeyReleaseMask
        | StructureNotifyMask;

    m_window = XCreateWindow(m_display, root, 0, 0, m_width, m_height, 0,
        m_depth, InputOutput, m_visual,
        CWColormap | CWBackPixel | CWBorderPixel | CWEventMask, &attrs);
    if (m_window == 0)
        return Error::from_string_literal("X11Context: XCreateWindow failed");

    m_gc = XCreateGC(m_display, m_window, 0, nullptr);

    int bytes_per_pixel = m_depth / 8;
    size_t image_bytes = static_cast<size_t>(m_width) * m_height * bytes_per_pixel;

    // Native-format backing store for the XImage. Plain XPutImage is the default:
    // it is correct everywhere and we only blit dirty rects, so the copy cost is low.
    // XShm (zero-copy) is an optimization that does not work on every server (e.g.
    // some Xvfb builds), so it is opt-in via SERENADE_X11_USE_SHM=1.
    if (getenv("SERENADE_X11_USE_SHM") && XShmQueryExtension(m_display)) {
        int shmid = shmget(IPC_PRIVATE, image_bytes, IPC_CREAT | 0600);
        if (shmid != -1) {
            m_image_data = static_cast<unsigned char*>(shmat(shmid, nullptr, 0));
            if (m_image_data != nullptr) {
                std::memset(m_image_data, 0, image_bytes);
                XShmSegmentInfo seg {};
                seg.shmid = shmid;
                seg.shmaddr = reinterpret_cast<char*>(m_image_data);
                seg.readOnly = False;
                m_image = XCreateImage(m_display, m_visual, m_depth, ZPixmap, 0,
                    reinterpret_cast<char*>(m_image_data), m_width, m_height, 32, 0);
                if (m_image != nullptr) {
                    m_image->xoffset = 0;
                    XShmAttach(m_display, &seg);
                    m_use_shm = true;
                } else {
                    shmdt(m_image_data);
                    shmctl(shmid, IPC_RMID, nullptr);
                    m_image_data = nullptr;
                }
            } else {
                shmctl(shmid, IPC_RMID, nullptr);
            }
        }
    }
    if (!m_use_shm) {
        m_image_data = static_cast<unsigned char*>(std::malloc(image_bytes));
        if (m_image_data == nullptr)
            return Error::from_errno(ENOMEM);
        std::memset(m_image_data, 0, image_bytes);
        m_image = XCreateImage(m_display, m_visual, m_depth, ZPixmap, 0,
            reinterpret_cast<char*>(m_image_data), m_width, m_height, 32, 0);
        if (m_image == nullptr)
            return Error::from_string_literal("X11Context: XCreateImage failed");
        m_image->xoffset = 0;
    }

    // The compositor's framebuffer. Zero-copy: it IS the shared store (both are
    // 4 bytes/pixel). Otherwise a separate ARGB32 buffer, converted on flush.
    if (m_zero_copy) {
        m_fb = m_image_data;
    } else {
        size_t fb_bytes = static_cast<size_t>(m_width) * m_height * sizeof(u32);
        m_fb = static_cast<unsigned char*>(std::malloc(fb_bytes));
        if (m_fb == nullptr)
            return Error::from_errno(ENOMEM);
        std::memset(m_fb, 0, fb_bytes);
    }

    // Tell the input mapper the screen size so it can normalize absolute mouse
    // coordinates. The window is a fixed resolution for now.
    x11_set_viewport(m_width, m_height);

    XMapWindow(m_display, m_window);
    put_rect(0, 0, m_width, m_height);
    flush();

    return {};
}

void X11Context::convert_rect(int x, int y, unsigned width, unsigned height)
{
    // Standard little-endian TrueColor: ARGB32 is [B,G,R,A] and a 24-bit pixel is
    // [B,G,R], so this drops the alpha byte. (Non-standard masks are out of scope.)
    int bpp = m_depth / 8;
    for (unsigned row = 0; row < height; ++row) {
        const unsigned char* src = m_fb + (static_cast<size_t>(y + row) * m_width + x) * sizeof(u32);
        unsigned char* dst = m_image_data + (static_cast<size_t>(y + row) * m_width + x) * bpp;
        for (unsigned col = 0; col < width; ++col) {
            if (bpp >= 3) {
                dst[0] = src[0]; // B
                dst[1] = src[1]; // G
                dst[2] = src[2]; // R
            }
            src += sizeof(u32);
            dst += bpp;
        }
    }
}

void X11Context::put_rect(int x, int y, unsigned width, unsigned height)
{
    if (m_display == nullptr || m_image == nullptr)
        return;
    // Drain pending events before blitting. Reading them keeps the X protocol from
    // back-pressuring: unread responses would fill the socket and stall our PutImages
    // during continuous rendering. Crucially this DELIVERS input events rather than
    // dropping them -- during a drag the compositor flushes constantly, so discarding
    // here would eat the very button/motion events the input pump is meant to handle.
    pump_events();
    blit(x, y, width, height);
}

void X11Context::blit(int x, int y, unsigned width, unsigned height)
{
    if (m_display == nullptr || m_image == nullptr)
        return;
    if (!m_zero_copy)
        convert_rect(x, y, width, height);
    if (m_use_shm)
        XShmPutImage(m_display, m_window, m_gc, m_image, x, y, x, y, width, height, False);
    else
        XPutImage(m_display, m_window, m_gc, m_image, x, y, x, y, width, height);
}

void X11Context::flush()
{
    if (m_display != nullptr)
        XFlush(m_display);
}

void X11Context::start_event_pump()
{
    if (m_display == nullptr)
        return;
    // Set the pump up once we are actually running inside the server's event loop.
    // Notifiers created earlier (during open_device) are not serviced in this build,
    // whereas ones created from a deferred job (like the screenshot notifier) are.
    Core::EventLoop::current().deferred_invoke([this] {
        if (m_wake_pipe[0] >= 0)
            return;
        if (::pipe(m_wake_pipe) != 0)
            return; // No input pump without a pipe; rendering still works via put_rect draining.
        ::fcntl(m_wake_pipe[0], F_SETFL, O_NONBLOCK);

        // A Notifier on the (pollable) self-pipe wakes the main event loop. This works
        // where a Notifier directly on the X fd does not: Xlib buffers events in userspace,
        // so the X fd is not reliably "readable" for poll. The pump thread writes here when
        // there is X data to process.
        m_pump_notifier = Core::Notifier::construct(m_wake_pipe[0], Core::Notifier::Type::Read);
        m_pump_notifier->on_activation = [this] {
            char drain[64];
            while (::read(m_wake_pipe[0], drain, sizeof(drain)) > 0)
                continue;
            pump_events();
        };

        m_pumping = true;
        // Detached: the WindowServer is always terminated externally, so there is no clean
        // point to join it (and a joinable member would risk std::terminate on exit).
        std::thread([this] { pump_thread_main(); }).detach();
    });
}

void X11Context::pump_thread_main()
{
    int fd = ConnectionNumber(m_display);
    while (m_pumping) {
        fd_set rfds {};
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv { 0, 30000 }; // 30 ms safety-net interval
        int rc = select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (rc > 0 && FD_ISSET(fd, &rfds)) {
            char c = 'x';
            (void)!::write(m_wake_pipe[1], &c, 1);
        }
    }
}

void X11Context::pump_events()
{
    if (m_display == nullptr)
        return;
    while (XPending(m_display)) {
        XEvent ev {};
        XNextEvent(m_display, &ev);
        RawX11Event re {};
        switch (ev.type) {
        case Expose:
            // Window was cleared/mapped; repaint the exposed region from our store.
            blit(ev.xexpose.x, ev.xexpose.y, ev.xexpose.width, ev.xexpose.height);
            break;
        case MotionNotify:
            re.type = RawX11Type::Motion;
            re.x = ev.xmotion.x;
            re.y = ev.xmotion.y;
            x11_deliver_raw(re);
            break;
        case ButtonPress:
            re.type = RawX11Type::ButtonDown;
            re.x = ev.xbutton.x;
            re.y = ev.xbutton.y;
            re.button = ev.xbutton.button;
            x11_deliver_raw(re);
            break;
        case ButtonRelease:
            re.type = RawX11Type::ButtonUp;
            re.x = ev.xbutton.x;
            re.y = ev.xbutton.y;
            re.button = ev.xbutton.button;
            x11_deliver_raw(re);
            break;
        case KeyPress:
            re.type = RawX11Type::KeyDown;
            re.key = raw_key_for(ev.xkey, re.key_char);
            re.state = ev.xkey.state;
            x11_deliver_raw(re);
            break;
        case KeyRelease:
            re.type = RawX11Type::KeyUp;
            re.key = raw_key_for(ev.xkey, re.key_char);
            re.state = ev.xkey.state;
            x11_deliver_raw(re);
            break;
        default:
            // Not input we care about (MapNotify, ConfigureNotify, ...). Consuming it
            // keeps the X queue from backing up.
            break;
        }
    }
}

}
