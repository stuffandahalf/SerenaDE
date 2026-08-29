/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// screenshot-test (M2 -- scaffold placeholder, not yet built).
//
// Flow once enabled:
//   1. Start WindowServer headless (virtual screen backend) in-process or as a
//      child.
//   2. Launch the app under test via the normal LibGUI path.
//   3. Drive it with SyntheticInputBackend (deterministic script).
//   4. Capture the framebuffer, write PNG to a temp path.
//   5. Compare against tests/golden/<app>-<scenario>.png (exact match first;
//      if font hinting proves flaky across hosts, fall back to a documented
//      perceptual threshold -- record the decision in docs/PORTING.md).

int main()
{
    // TODO(M2)
    return 1;
}
