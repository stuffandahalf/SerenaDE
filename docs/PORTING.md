# Porting status & log

Living tracker. Update in the same commit as the work it describes, and keep
"Current status" in AGENTS.md consistent with this file.

## Component status

| Component                          | Milestone | Status      | Notes |
| ---------------------------------- | --------- | ----------- | ----- |
| Serenity pin (FetchSerenity + CI)  | M0        | done        | `7784b1f535` in both places; CI job unrun (no push yet) |
| Lagom build of LibGUI              | M0        | done        | Patch 0002; full lib links as `liblagom-gui.so` |
| ctest fully green                  | M0–M2     | done*       | 242/242 serial incl. M1 slice + 4 M2 golden tests; `-j` parallel is flaky (see log) — run serially |
| Headless vertical slice (1 app → PNG) | M1      | done        | WindowServer + AnalogClock + Clipboard → PNG; ctest `m1-headless-analog-clock-screenshot`; pristine-verified |
| Synthetic input + golden tests     | M2        | done        | FIFO input root (patch 0008); 4 deterministic goldens across Calculator/About/resize-fixture; pristine-verified |
| X11 screen backend                 | M3        | not started | XShm dirty-rect flush |
| X11 input backend                  | M3        | not started | Only real input backend (no evdev, by design) |
| ConfigServer / Clipboard           | M4        | partial     | Both build natively now (Clipboard via patch 0007); launcher runs them as services; no SystemServer yet |
| SystemServer shim + LaunchServer   | M4        | partial     | LaunchServer builds natively (patch 0009) and runs as a Calculator dependency; SystemServer shim still to do |
| Launcher + resource env            | M4        | partial     | Headless launcher complete (socket takeover, services, screenshot); X11 session launch still to do |
| App subset (Terminal, FileManager, Settings, ImageViewer, PixelPaint) | M4 | not started | |
| FreeBSD support                    | M5        | not started | X-input path only; no evdev anywhere |
| NetworkServer / AudioServer shims  | M6        | not started | Unblocks Browser/Mail/games |

## Porting log

Record discoveries here as they happen (surprising `#ifdef` gaps, API quirks,
decisions with trade-offs). Newest first.

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
