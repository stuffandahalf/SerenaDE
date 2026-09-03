# Porting status & log

Living tracker. Update in the same commit as the work it describes, and keep
"Current status" in AGENTS.md consistent with this file.

## Component status

| Component                          | Milestone | Status      | Notes |
| ---------------------------------- | --------- | ----------- | ----- |
| Serenity pin (FetchSerenity + CI)  | M0        | done        | `7784b1f535` in both places; CI green on pinned Clang 22 (apt.llvm.org) |
| Lagom build of LibGUI              | M0        | done        | Patch 0002; full lib links as `liblagom-gui.so` |
| ctest fully green                  | M0–M2     | done*       | 242/242 serial incl. M1 slice + 4 M2 golden tests; `-j` parallel is flaky (see log) — run serially |
| Headless vertical slice (1 app → PNG) | M1      | done        | WindowServer + AnalogClock + Clipboard → PNG; ctest `m1-headless-analog-clock-screenshot`; pristine-verified |
| Synthetic input + golden tests     | M2        | done        | FIFO input root (patch 0008); 4 deterministic goldens across Calculator/About/resize-fixture; pristine-verified |
| X11 screen backend                 | M3        | done        | `X11ScreenBackend` renders the compositor's ARGB32 buffer to a fullscreen X window; dirty-rect blit is `XPutImage` (default) with zero-copy/`XShmPutImage` opt-in via `SERENADE_X11_USE_SHM`; patch 0012 wires `Mode=X11`; verified headless vs 24-bit Xvfb |
| X11 input backend                  | M3        | done        | Real mouse (motion+buttons) and keyboard → `ScreenInput` via a pump thread + self-pipe notifier; two-TU split around the Xlib/Serenity `KeyCode` clash; verified motion moves cursor, button press/drag/release delivered, keys typed |
| ConfigServer / Clipboard           | M4        | partial     | Both build natively now (Clipboard via patch 0007); launcher runs them as services; no SystemServer yet |
| SystemServer shim + LaunchServer   | M4        | partial     | LaunchServer builds natively (patch 0009) and runs as a Calculator dependency; SystemServer shim still to do |
| Launcher + resource env            | M4        | partial     | Headless launcher complete (socket takeover, services, screenshot) plus `--x11` mode (writes `Mode=X11`, passes `$DISPLAY` through) and `--home <dir>` (sets `$HOME` for spawned apps); SystemServer shim still to do |
| App subset (Terminal, FileManager, Settings, ImageViewer, PixelPaint) | M4 | partial | Terminal + FileManager build & render on host (patches 0013–0018) with golden tests; cross-app copy/paste via clip-copy/clip-paste + launcher `--co-app` (`m4-clipboard-cross-app`); launch-from-desktop proven functionally via `launch-terminal` fixture + LaunchServer + `--expect-window` (`m4-launch-terminal`). All 3 M4 exit criteria pass. Settings/ImageViewer/PixelPaint not started; literal Taskbar UI is a follow-up |
| FreeBSD support                    | M5        | not started | X-input path only; no evdev anywhere |
| NetworkServer / AudioServer shims  | M6        | not started | Unblocks Browser/Mail/games |

## Porting log

Record discoveries here as they happen (surprising `#ifdef` gaps, API quirks,
decisions with trade-offs). Newest first.

- 2026-09-02 — **M4 checkpoint: Terminal + FileManager run natively on host.** Two
  flagship apps now build via Lagom and render real content headlessly, each behind a
  deterministic golden test (`m4-terminal-echo`, `m4-filemanager-docs`); full serial
  ctest 244/244. Patches 0013–0018; the launcher gains `--home <dir>` (sets `$HOME` for
  spawned apps, needed by FileManager/config reads). Findings:
  - **FileManager's heavy dep chain was already built.** It links LibArchive/Audio/
    Config/PDF/Threading/FileSystem/Maps — all but **LibMaps** were already in
    `lagom_standard_libraries`, and its `FileOperation` worker service is built by the
    services component. So wiring it in = add `Maps` + one `add_serenity_subdirectory`.
  - **Only three host fixes, all small.** (1) `#include <serenity.h>` → guard with
    `AK_OS_SERENITY` (the other POSIX includes cover everything on host). (2) two
    `disown(child)` calls (Serenity-only LibC that detaches a spawned child) → guard.
    (3) **GML include path**: `stringify_gml()` emits `*GML.h` into the target's binary
    dir, and FileManager includes them as `<Applications/FileManager/...>` — that root is
    not on the host include path (unlike a *library*'s GML, e.g. `<LibGUI/...>`, which
    resolves via `.../Userland/Libraries`). Add `${CMAKE_CURRENT_BINARY_DIR}/../..` to
    the target. No other app uses GML yet, so this was the first hit.
  - **The golden compare is not strict-zero.** `compare_golden` passes when ≤0.1% of
    pixels exceed the per-channel tolerance (default 16) — so FileManager's ~40px of
    anti-aliasing drift between runs passes with ~20× headroom, while a truly broken
    render (thousands of px) still fails. Deterministic content (Calculator/About/Terminal)
    lands at AE=0; complex renders get the sliver.
  - **Terminal's ctest "failure" was a stale diagnostic artifact, not a bug.** A leftover
    `/tmp/envwrap.sh` wrapper in the test command (from earlier env-bisection) had been
    deleted out from under it; with a clean `serenade-launcher` invocation and a 7s delay
    the Terminal golden is byte-stable (AE=0). Lesson: keep test commands self-contained —
    no `/tmp` scratch wrappers referenced from CMake.
  - **Cross-app copy/paste is proven with two fixture processes + `--co-app`.** The
    launcher gained `--co-app <binary>` (+`--co-app-delay <ms>`): it spawns a second app
    against the *same* WindowServer after letting the primary act first. Two tiny SerenaDE
    fixtures (`src/TestApps/clip_copy.cpp`, `clip_paste.cpp`) do the round-trip over the
    real Clipboard IPC service — clip-copy publishes "SERENADE_CLIP_OK", clip-paste fetches
    and renders it. No Serenity patch is involved (both live in this repo). The golden is
    byte-stable (AE=0); a negative control (paster alone → "(empty)") differs in the label
    region, confirming the text genuinely crossed processes. `m4-clipboard-cross-app`.
  - **Launch-from-desktop is proven functionally (no new Serenity patch).** The
    `launch-terminal` fixture (src/TestApps) asks LaunchServer to open the Terminal
    executable via `Desktop::Launcher::open()` — the same IPC path a desktop menu click
    takes. LaunchServer's `open_file_url` spawns any regular executable directly
    (`Core::Process::spawn`), so no `.app` registration or app-dir redirection is needed.
    The launched terminal is interactive (prompt + rc files) → non-deterministic pixels,
    so it can't be a golden; instead the launcher's new `--expect-window <min-fraction>`
    asserts a window with content actually appeared (dominant-color non-bg fraction). A
    negative control (non-executable target → no window) fails the check, confirming it is
    not a false positive. `m4-launch-terminal`. All three M4 exit criteria now pass; the
    literal Taskbar/desktop UI (a real menu to click) remains a follow-up.

- 2026-09-01 — **M3 complete: real X11 screen + input backends.** WindowServer now
  renders to a live `$DISPLAY` and accepts real mouse/keyboard, via a third
  `ScreenBackend` (`X11ScreenBackend`) selected by a new `Mode=X11` (patch 0012). All
  Xlib lives in SerenaDE's `src/WindowServerX11`; the Serenity tree only gains a
  forward-declaring hook header (`SerenadeX11.h`) and the mode plumbing — no Xlib, no
  compositor changes. Verified headless against a 24-bit Xvfb (desktop + About window
  match the golden colors; motion moves the cursor; a full press→drag→release is
  delivered to `ScreenInput` with correct button bits). Findings:
  - **Xlib's `KeyCode` typedef collides with Serenity's `enum KeyCode`.** No TU may
    include both, so the input path is two TUs meeting at neutral `RawX11Event` records:
    `X11Context.cpp` (Xlib only) reads/decodes X events; `X11InputMap.cpp` (Serenity
    only) maps them to `::KeyEvent`/`MousePacket`. The neutral header must also avoid
    Xlib `#define`s that shadow identifiers (`ButtonPress`, `KeyPress`, `None` are all
    macros), so its enumerators use different names.
  - **The blit path must *deliver* input events, not drop them.** `put_rect()` drains
    pending X events before each flush (required — unread responses back-pressure the
    socket during continuous rendering). During a drag the compositor flushes on every
    frame, so a drain-and-discard silently eats the very button/motion events the pump
    is meant to handle; motion-only tests pass because they have no concurrent flush.
    The drain now routes input to the mapper and repaints Expose regions.
  - **A `Core::Notifier` on the X connection fd does not fire reliably**: Xlib buffers
    events in userspace, so `XPending>0` does not imply the fd is poll-readable. A pump
    thread `select()`s the fd (30 ms safety-net timeout) and writes to a self-pipe; a
    pipe `Notifier` wakes the main loop, which reads + delivers on the main thread
    (Xlib/ScreenInput are not thread-safe). The pump is created from a `deferred_invoke`
    because Notifiers made during `open_device()` (before `loop.exec()`) are not serviced.
  - **Use the screen's default visual/depth, not a deeper one.** This Xvfb is 24-bit
    only; a 32-bit *visual* exists on it but a 32-bit window renders all-black. We take
    the zero-copy fast path only when the default is a matching 32-bit TrueColor, else
    convert ARGB32→native per rect. `XShmPutImage` is broken on this Xvfb (black), so
    plain `XPutImage` is the default and SHM is opt-in (`SERENADE_X11_USE_SHM=1`).
  - **Link-order gotcha:** `serenade_x11` is defined under `src/`, which is added after
    Lagom, but the `WindowServer` target is created *during* Lagom. So WindowServer's own
    CMakeLists cannot link it (an unknown name is treated as a raw `-l`); the link is done
    in the top-level `CMakeLists.txt` once both targets exist.

 - 2026-08-31 — **CI toolchain mismatch: pinned CI to Clang 22.** The first real CI
   runs (GitHub `ubuntu-latest` = Ubuntu noble) failed in AK for a compiler-*version*
   gap, not a SerenaDE bug: local dev is Arch (Clang 22.1.8), while noble's apt ships
   Clang 18. Two distinct errors surfaced on the older Clang:
   - `-Winvalid-constexpr` on `seconds_since_epoch_to_year()` (`AK/Time.h`): it
     structured-binds the non-literal `Tuple` from `days_since_epoch_to_date()`, so it
     can never be a constant expression; Clang 18 diagnoses that, Clang 22 doesn't.
   - `use of undeclared identifier '__GCC_DESTRUCTIVE_SIZE'` (`AK/Platform.h` →
     `SharedCircularQueue.h`): `AK_SYSTEM_CACHE_ALIGNMENT_SIZE` is `__GCC_DESTRUCTIVE_SIZE`,
     a builtin Clang only defines in newer versions (22 defines it as 64; 18 does not).
   Root-cause fix: **pin the CI compiler to Clang 22** via apt.llvm.org (ships 22.1.8,
   matching dev) rather than patching Serenity files one-by-one — that second error was
   the tell that whack-a-mole was the wrong strategy. `ci.yml` now installs
   `clang-22`/`lld-22` from apt.llvm.org (dynamic `lsb_release -cs` codename) and configures
   with `-DCMAKE_CXX_COMPILER=clang++-22`. As defense-in-depth we also kept **patch 0011**
   (drop the dead `constexpr`, add `inline`) so that specific case is correct even on an
   older Clang. Lesson: a from-scratch "CI simulation" built with the *local* toolchain is
   misleading — it only proves the patch set applies/builds locally, not on CI's compiler;
   pinning the toolchain makes the two agree.

- 2026-08-31 — **M2 complete: synthetic input + golden-screenshot harness.**
  Programmatic click/drag/resize work; four deterministic golden tests pass across
  three apps (Calculator clicks → "3", About idle render, resize-fixture move+resize).
  Full serial ctest 242/242; the whole 10-patch set was re-verified from a pristine
  checkout of the pin (apply → build → M2 slice). Findings:
  - **Input is injected through FIFOs, not a new IPC socket.** Patch 0008 adds
    `$WINDOW_SERVER_INPUT_ROOT` and makes the device scan accept FIFOs (in addition
    to char/block devices). The launcher writes raw `KeyEvent`/`MousePacket` structs
    into per-device FIFOs; WindowServer's existing drain code reads them unchanged.
    No WindowServer IPC surface changed — much smaller blast radius than a bespoke
    input socket.
  - **Absolute mouse coords are 16-bit scaled.** `Screen.cpp` maps the wire value
    as `x * width / 0xffff`, so the launcher must convert screen pixels to the wire
    range (`x * 0xffff / screen_width`). Forgetting this is the #1 "input does
    nothing" trap — the cursor lands in a corner.
  - **FIFO open mode + ordering matter on Linux.** `O_WRONLY|O_NONBLOCK` returns
    ENXIO while no reader is attached; open the write ends with `O_RDWR|O_NONBLOCK`.
    And the FIFOs must exist *before* WindowServer starts (its device scan runs at
    construction and there is no devicemap rescan on hosts).
  - **The granular `mouse move` command releases a held button** (it sends
    `buttons=0`), which aborts a mid-drag. Use the built-in `mouse drag` (which holds
    the button) for move/resize; start the drag *on* the frame border — interior
    points are content hits routed to the client, not the frame handler.
  - **Real apps are poor move/resize targets.** AnalogClock and Calculator call
    `set_resizable(false)`; About is a non-Normal dialog whose `WindowFrame` returns
    early for frame events. So the M2 fixture `resize-test-window` (a normal,
    resizable window at a fixed position) is what proves move/resize. It also logs its
    client rect to stderr so scripts can target the titlebar/border precisely.
  - **Calculator needs LaunchServer running** (LibDesktop IPC at
    `/tmp/session/0/portal/launch`). Patch 0009 builds it as a Lagom service; the
    golden-test CMake wires it in as an extra `--service` for that one test.
  - **About's GML uses an absolute `/res/...` bitmap path**, bypassing the
    `$SERENITY_RES` ResourceImplementation override. Patch 0010 makes `Core::File`
    remap a read-only `/res/*` open to `$SERENITY_RES` on ENOENT (no-op on real
    Serenity, where `/res` exists).
  - **AnalogClock is deliberately not a golden target**: it renders wall-clock time,
    so its pixels drift between capture and re-run (a full clock-face's worth of
    differing pixels). Golden targets must be deterministic.
  - **Pristine verification caught two stale hunks** the dev tree had drifted past:
    0008 returned a bare `const char*` where a `StringView` is required (AK has no
    implicit `const char*`→`StringView`; use `{ptr, strlen(ptr)}`), and 0009 carried
    a spurious LibDesktop `compile_ipc` block that collided with the LaunchServer
    service's generated `LaunchServerEndpoint.h` target. Both regenerated from the
    dev tree's final state; the patched tree is now byte-identical to it.

- 2026-08-30 — **M1 complete: headless vertical slice.** WindowServer (virtual
  screen) + Clipboard + AnalogClock run natively over plain UDS; SIGUSR1 dumps
  a verified PNG of the rendered app. Patches 0003–0007, launcher rewritten.
  Exit criteria met: `ctest -R m1-headless` passes in ~3s, full serial ctest
  238/238, and the whole patch set was re-verified on a pristine worktree of
  the pin (apply → build → slice run). Findings:
  - **`SOCKET_TAKEOVER` is the fd-passing hook.** Serenity services never bind
    their own sockets; SystemServer pre-binds them and hands fds over via the
    `SOCKET_TAKEOVER=path:fd[;path:fd]` env var (parsed in
    `LibCore/SystemServerTakeover.cpp`). The launcher replicates this: it
    binds `/tmp/portal/window`, `/tmp/portal/wm`, and each service's socket,
    then spawns each child with the right takeover value. No Serenity code
    needed changing for IPC itself.
  - **Config files must pre-exist and use `Key=Value`.** `ConfigFile::open`
    returns ENOENT even with `AllowWriting::Yes`, and its parser does not trim
    whitespace from keys (`Mode = Virtual` stores the key `"Mode "`). The
    launcher therefore writes a minimal virtual-screen INI itself.
  - **WindowServer startup assumes kernel facilities** (patch 0005 makes each
    tolerant instead of fatal): TTY graphics ioctls, `/dev/gpu` enumeration
    (fallback screen layout), devicemap inotify watches, `/etc/Keyboard.ini`,
    and `/sys/kernel/keymap`. All are now skipped/degraded gracefully on hosts.
  - **Resource paths**: `resource://` URIs resolve through
    `ResourceImplementation::the()`, which now honors `$SERENITY_RES` (patch
    0004). WindowServer's raw `/res/...` literals (~10 sites: themes, icons,
    cursors) go through a new `WindowServer::res_path()` helper.
  - **Two nasty host-portability bugs found by instrumenting, not reading:**
    (a) CPython sets `FD_CLOEXEC` on sockets created via its API — a Python
    debug harness silently lost the takeover fds at exec; plain-C `socket()`
    does not, so the real launcher was unaffected. (b) A `Core::Notifier`
    held in a block-scoped `RefPtr` is destroyed before `loop.exec()`, which
    unregisters its fd from the poll set — the screenshot notifier had to be
    hoisted to function scope. Also: `Core::Timer::create_single_shot` does
    **not** auto-start; callers must invoke `start()`.
  - **Lagom now builds WindowServer, Clipboard, and AnalogClock unmodified**
    (patch 0007). Details: Keyboard added to the standard lib list; `ASM`
    language enabled (app icons embed as `.s` objects); a shim
    `sys/devices/gpu.h` in the binary dir provides the GPU structs from a new
    `GpuTypesHost.h` plus the original inline ioctl wrappers, because pulling
    in Serenity's real `gpu.h` drags `Kernel/API/Ioctl.h`, whose enum members
    collide with host ioctl macros.
- 2026-08-29 — **M0 complete.** Pinned Serenity to `7784b1f535` and built the
  full LibGUI natively via Lagom (patches 0001 + 0002). Findings:
  - The long tail was tiny: **one** source fix for all 124 LibGUI translation
    units (`Window.cpp` needed `<limits.h>` for `INT_MIN`; it had relied on a
    transitive Serenity-LibC include). Everything else — GML codegen, IPC
    endpoint generation, the whole widget stack — compiled and linked as-is.
  - Lagom previously carried a *partial* six-file LibGUI target (for tooling);
    patch 0002 replaces it with the full library via the standard-library list.
    `LibConfig` also had to be added to that list (LibGUI links it).
  - IPC endpoint headers for Clipboard/NotificationServer/WindowServer are now
    generated from LibGUI's own CMakeLists under `if (NOT SERENITYOS)`,
    following the existing LibImageDecoderClient pattern.
  - SerenaDE-side fix: `ApplyPatches.cmake` had a wrong glob path (patches were
    silently not applied) and is now idempotent — it detects already-applied
    patches via reverse check, so dev trees with manual edits still configure.
- 2026-08-29 — Scaffold verified end-to-end against local Serenity checkout
  (Clang 22): configure + full build green (1598 targets), ctest 236 passed /
  1 failed / 1 skipped. Findings:
  - **Lagom already builds most of the core stack natively**: 50 libraries
    (incl. LibGfx, LibIPC, LibCore, LibHID, LibAudio, LibGPU/SoftGPU), several
    services (ConfigServer, EchoServer, FileOperation, ImageDecoder,
    LookupServer, RequestServer, SQLServer, SSHServer, WebServer), Shell, and
    ~300 utilities. M0 is therefore "add LibGUI + pin", not "port the world".
  - **GCC 16 cannot build this**: `-Werror=stringop-overflow` false positive on
    `AK::Vector.h:630` (placement-new into heap slot) via LibCompress LZW, hit
    while compiling `LibGfx/ImageFormats/GIFLoader.cpp`. Clang 22 builds clean.
    Candidate upstream bug report; SerenaDE standardizes on Clang meanwhile.
  - **ctest is green serially: 237/237.** With `-j8`, different Shell tests
    fail on each run (`Shell-builtin-redir`, `Shell-control-structure-as-command`,
    `TestSqlDatabase` ILLEGAL) while all pass individually and in a full serial
    run. Diagnosis: the Shell test suite shares working-directory state between
    concurrently running tests (redir failures show one test's files missing
    mid-run). Run `ctest` **without `-j`** for reliable results; candidate
    upstream issue (per-test WORKING_DIRECTORY) — file a PR if it reproduces.
  - Lagom must not receive `-DCMAKE_BUILD_TYPE` (its root project rejects it);
    SerenaDE top-level sets C++26 + `-fno-exceptions` for its own targets to
    match Lagom's directory-scoped settings.
