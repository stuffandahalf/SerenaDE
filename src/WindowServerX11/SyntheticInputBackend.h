/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// Scripted input backend for headless testing (M2).
//
// Feeds a queued list of KeyEvent/MousePacket into the InputSink on demand --
// no host device access at all, so it runs in CI without X. This is what makes
// golden-screenshot tests deterministic: same script in, same pixels out.
//
// Deliberately header-only for now; give it a .cpp if the queueing logic grows.

#include <AK/Queue.h>
#include <AK/Vector.h>
#include "InputBackend.h"

namespace SerenaDE {

class SyntheticInputBackend final : public InputBackend {
public:
    // Enqueue events to be delivered one per start()/drain() cycle.
    void queue_keyboard(KeyEvent const& event) { m_keyboard.enqueue(event); }
    void queue_mouse(MousePacket const& packet) { m_mouse.enqueue(packet); }

    virtual void start(InputSink&, Core::EventLoop&) override { }
    virtual void stop() override { }

    // Test harness pulls from here; not part of the InputBackend contract.
    bool has_pending() const { return !m_keyboard.is_empty() || !m_mouse.is_empty(); }
    Optional<KeyEvent> take_keyboard() { return m_keyboard.dequeue_optional(); }
    Optional<MousePacket> take_mouse() { return m_mouse.dequeue_optional(); }

private:
    Queue<KeyEvent> m_keyboard;
    Queue<MousePacket> m_mouse;
};

}
