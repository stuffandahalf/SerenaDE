# AGENTS.md — SerenaDE

Guidance for AI agents (and humans) working in this repository. Read this file
before making changes.

## What this project is

SerenaDE runs the **Serenity OS desktop environment** — WindowServer, LibGUI,
LibGfx and the userland applications — natively on **Linux and BSDs**, using
**X11** for display and input.

This repository is the *shim*. It contains only what cannot live upstream:
platform-specific backends, host service shims, the launcher, tests, CI, and a
minimal tracked patch set applied to a pinned Serenity source tree. Everything
else (AK, LibCore, LibGfx, LibGUI, WindowServer, the applications) is built
from the Serenity tree itself via its official host-build system, **Lagom**
(`Meta/Lagom/` in the Serenity repo).

## Ground rules

1. **Portability first.** Linux and BSDs (FreeBSD at minimum) are equal
   targets. No Linux-specific API outside an explicitly named backend
   (`X11*`; a future `Evdev*` would be the exception that must be argued for in
   a PR). Prefer POSIX; when a portable path and a faster platform-specific one
   both exist, take the portable one and note the trade-off in
   `docs/PORTING.md`.
2. **Hybrid upstream strategy.** Generic portability fixes (missing
   `AK_OS_*` guards, Lagom CMake support for more libraries) become PRs to
   `SerenityOS/serenity`, not patches here. Platform glue (X11, host process
   management, launcher) stays in this repo. See "Upstreaming policy".
3. **Pinned dependency.** SerenaDE only builds against a pinned Serenity ref
   (`cmake/FetchSerenity.cmake` + the CI checkout). Bump the pin deliberately:
   own commit, all patches rebased, CI green. Never build against a branch head
   casually.
4. **The patch set must always apply.** `patches/*.patch` are applied at
   configure time; if any fails `git apply --check`, the build stops. The
   directory's long-term target is *empty*.
5. **Headless before X11, vertical slice before breadth.** Never debug an X11
   symptom that a headless run (virtual screen + synthetic input) cannot
   reproduce. Get one app pixel-correct before touching the next.
6. **Milestones are sequential.** Each milestone below has exit criteria. Do
   not start M(n+1) work until M(n)'s exit criteria are met and committed.

## Repository layout

```
SerenaDE/
├── AGENTS.md                  # This file: structure, rules, procedure
├── README.md                  # Human-facing intro
├── LICENSE                    # BSD-2-Clause (matches Serenity)
├── CMakeLists.txt             # Top-level: pin Serenity, build Lagom, add src/tests
├── cmake/
│   ├── FetchSerenity.cmake    # Pinned-ref fetch when no local checkout given
│   └── ApplyPatches.cmake     # Applies patches/*.patch; fails configure on drift
├── patches/                   # Minimal tracked in-tree modifications (goal: empty)
│   └── README.md              # Patch policy + workflow
├── src/
│   ├── CMakeLists.txt         # AK-header compile flags; adds the subdirs below
│   ├── WindowServerX11/       # The core of the shim
│   │   ├── InputBackend.h     # Platform-neutral input seam (the interface)
│   │   ├── X11ScreenBackend.* # ScreenBackend impl: X window + XShm dirty-rect flush
│   │   ├── X11InputBackend.*  # X events -> KeyEvent/MousePacket (only real backend)
│   │   └── SyntheticInputBackend.h  # Scripted input for headless tests
│   ├── Services/
│   │   └── SystemServerShim/  # Same IPC contract, fork/exec of host processes
│   └── Launcher/              # serenade-launcher: env setup + service startup order
├── tests/
│   ├── screenshot_test.cpp    # M2 golden-screenshot harness (headless)
│   └── golden/                # Reference PNGs
├── docs/
│   └── PORTING.md             # Component status tracker + porting log
└── .github/workflows/ci.yml   # Linux build+test; the FreeBSD VM job returns as M7's first task
```

Serenity-side reference points (in the pinned Serenity tree):

- `Userland/Services/WindowServer/` — compositor, `ScreenBackend` interface
  (`Screen.cpp::open_device()` picks Device vs Virtual backend), input drain
  (`EventLoop.cpp` → `ScreenInput::on_receive_keyboard_data/mouse_data`).
- `Kernel/API/KeyCode.h` (`KeyEvent`) and `Kernel/API/MousePacket.h` — the two
  structures all input is translated into.
- `Userland/Libraries/LibCore/Socket.cpp` — IPC transport (UDS + FD passing).
- `Meta/Lagom/` — host build system; `ReadMe.md` documents external use.

## Building

```sh
# Development (preferred): local Serenity checkout
cmake -S . -B Build -G Ninja \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DSERENITY_SOURCE_DIR=~/git/serenity
cmake --build Build
ctest --test-dir Build --output-on-failure   # serial: do NOT add -j (see M0)

# No local checkout: a pinned ref is fetched (slow; see cmake/FetchSerenity.cmake)
cmake -S . -B Build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
```

- **Clang is required.** Do not pass `CMAKE_BUILD_TYPE` (Lagom owns build
  configuration). GCC ≥ 12 is Lagom's minimum, but current GCC (16) triggers a
  `-Werror=stringop-overflow` false positive in `AK::Vector.h` via LibCompress
  LZW — use Clang (see porting log in `docs/PORTING.md`).
- X11 dev packages (`libX11`, `libXext`) are needed from M3 onward.
- `cmake --build Build` must stay green at all times, including scaffold stubs.

## Architecture decisions

Recorded *why*, so they are not re-litigated casually. Change one only by
updating this section in the same commit.

- **IPC is unchanged.** Serenity's entire client/server stack already runs on
  `Core::LocalSocket` = POSIX UDS with FD passing (`SCM_RIGHTS`). It works on
  Linux and BSDs without modification. Do not invent a new transport.
- **Screen: `X11ScreenBackend` implements WindowServer's existing
  `ScreenBackend` interface.** One X window (fullscreen default), XShm backing
  store, dirty-rect flush via `XShmPutImage`. Hooked in through a tracked patch
  that adds a new `ScreenLayout::Screen::Mode` — mirroring the existing
  Device/Virtual cases in `Screen::open_device()`. No GPU: the compositor is
  software-rendered (LibGfx CPU rasterizer); acceleration is out of scope.
- **Input: `InputBackend` interface; X11 is the only real backend.** evdev was
  considered and rejected: it is Linux-only, adds a permission model, and would
  force two maintained input paths. Xlib events are uniform on every target OS
  and add no new dependency (X is already required for output). The `Synthetic`
  backend exists for deterministic headless tests. All backends translate into
  the existing `KeyEvent`/`MousePacket` structures; WindowServer's own
  `/dev/input/*` scanning stays dormant on the host.
- **Event loop:** `Core::EventLoop` already has a Unix/poll implementation
  (`EventLoopImplementationUnix.cpp`). The X connection fd is registered as a
  `Core::Notifier`; no custom loop.
- **SystemServer shim, not app patches.** Apps launch processes via the
  SystemServer IPC contract; the shim binds that endpoint and fork/execs host
  binaries. App code stays unmodified.
- **Resources** (`Base/res`: fonts, icons, GML, themes, keymaps) are located
  via environment variables set by `serenade-launcher`, not hardcoded paths.

## Implementation procedure

Milestones are sequential; each lists tasks and **exit criteria**. Update
"Current status" below and `docs/PORTING.md` in the same commit as the work.

### M0 — Native build of the core libraries

Verified at scaffold time: Lagom **already** builds 50 libraries natively,
including `LibGfx`, `LibIPC`, `LibCore`, `LibHID`, `LibAudio` — plus several
services (notably `ConfigServer`) and ~300 utilities. The remaining gap is
smaller than it looks:

- Pin the first known-good Serenity ref (release tag or commit) in
  `cmake/FetchSerenity.cmake` **and** `.github/workflows/ci.yml`; make CI green
  with that pin.
- Extend Lagom to compile **LibGUI** for the host (the last core library): add
  the subdirectory, wire codegen outputs, fix the `AK_OS_*` long tail.
- Every generic fix goes to an upstream PR as soon as it is clean (see
  Upstreaming policy); only genuinely SerenaDE-specific needs become patches.

**Exit:** `cmake --build Build` compiles the LibGUI target on Linux in CI;
ctest fully green **run serially** (no `-j` — the Shell suite has shared-state
interference under parallel load, see porting log).

### M1 — Headless vertical slice (the big de-risk)

- Run WindowServer with the virtual screen backend against a host UDS; boot one
  trivial app (About or AnalogClock); dump the framebuffer to PNG.
- Resolve resource paths (fonts/icons/GML) via launcher env vars.

**Exit:** a visually correct screenshot of one real app, produced by a command
that runs in CI. This proves IPC-over-UDS (incl. FD passing), fonts/resources,
and the compositor end-to-end.

### M2 — Synthetic input + golden-screenshot harness

- Implement `SyntheticInputBackend` delivery into `ScreenInput`.
- Build `tests/screenshot_test.cpp`: drive an app with a scripted input
  sequence, capture PNG, compare against `tests/golden/`.

**Exit:** click/drag/resize work programmatically; ≥3 apps have passing
golden-screenshot tests in CI. This is the regression net for all later work.

### M3 — Real display + real input (Linux MVP)

- Implement `X11ScreenBackend` (window, XShm, dirty-rect flush, resize).
- Implement `X11InputBackend` per the translation table in its header.
- Tracked patch wiring both into WindowServer (new screen mode; backend library
  linked in).

**Exit:** usable fullscreen desktop on a Linux box — mouse moves the cursor,
keyboard works in a text field, windows can be dragged/resized with real input.

### M4 — Services + launcher → daily-drivable subset

- Implement `serenade-launcher` (env, `$HOME`, startup order: SystemServerShim
  → WindowServer → ConfigServer → Clipboard → LaunchServer → Taskbar; clean
  teardown).
- Port ConfigServer and Clipboard (near-as-is IPC servers).
- Implement SystemServerShim (spawn/kill/exit tracking) and LaunchServer.
- Enable apps by value-per-effort: **Terminal** (pty + text input),
  **FileManager** (icons, file ops, context menus), **Settings** (config +
  widget coverage), **ImageViewer**, **PixelPaint**.

**Exit:** launch Terminal from the desktop; copy/paste between two apps; browse
the real filesystem in FileManager. All M2 golden tests still pass.

### M5 — BSD support (FreeBSD) + polish

- Verify/configure on FreeBSD; fix any non-POSIX drift.
- DPI/scale factors, cursor themes, font coverage, window-manager edge cases
  (focus, multi-monitor later).

**Exit:** the M4 app subset builds, links and boots WindowServer on FreeBSD;
polish items done; Linux CI green. (Full-suite green on FreeBSD plus NetBSD
and OpenBSD support moved to M7.)

### M6 — Heavy apps

- NetworkServer shim (host sockets) → Browser, Mail, Maps.
- AudioServer shim (PulseAudio/PipeWire on Linux; treat BSD audio as
  best-effort/stub) → games, Piano.

**Exit:** Browser loads web pages; audio plays in at least one game.

### M7 — Full BSD support (FreeBSD, NetBSD, OpenBSD)

M5 closed with FreeBSD at "builds, links, boots; full suite unverified" and
the other BSDs untouched. One subtask per OS, each following the same shape:
portability audit of `src/` + patches → build → boot WindowServer → serial
ctest green (at minimum the M1–M4 slice).

- **FreeBSD:** re-add the `build-freebsd` CI job (vmactions/freebsd-vm on a
  hosted runner, as before 7ebcda8) and get the full serial ctest green in
  the VM. Patches 0026–0037 already cleared every known blocker.
- **NetBSD:** audit + build; expect `posix_spawn` feature gaps (the portable
  fallbacks from 0026/0027 should cover them), keymap/input quirks, and
  toolchain choices (pkg LLVM vs native).
- **OpenBSD:** audit + build; expect pledge(2) policy interactions with our
  no-op'd pledge calls, stricter W^X, and `sys/sysmacros.h` (covered by 0029).

**Exit:** CI green on Linux + FreeBSD + NetBSD + OpenBSD for the M4 app
subset; a porting-log entry per OS.

### Current status

**M2 — complete (2026-08-31).** Synthetic input + golden-screenshot harness work
end-to-end. Input is injected by feeding raw `KeyEvent`/`MousePacket` structs
through FIFOs under a private input root (patch 0008: `$WINDOW_SERVER_INPUT_ROOT`
+ accept FIFOs as input sources), so no new WindowServer IPC socket was needed;
the launcher (`src/Launcher`) replays scripts of `delay`/`mouse`/`key` commands,
converting absolute mouse coords to the wire's 16-bit range. Patches 0009–0010
extend Lagom to build LibDesktop, About, Calculator, and LaunchServer, and remap
read-only `/res/*` opens to `$SERENITY_RES` on hosts (About's GML references an
absolute `/res/...` bitmap path). Four deterministic golden tests pass across three
apps: Calculator (clicks compute 1+2=3), About (idle render), and the
`resize-test-window` fixture (titlebar-drag move + border-drag resize — real apps
either `set_resizable(false)` or are non-movable dialogs, so a purpose-built normal
window proves move/resize). AnalogClock is deliberately excluded: it renders
wall-clock time and is non-deterministic. Exit criteria met: full serial ctest
**242/242**, and the whole 10-patch set was re-verified from a pristine checkout of
the pin (apply → build → M2 slice) — that pass caught and fixed two stale hunks in
0008 (a `StringView` return) and 0009 (a spurious LibDesktop `compile_ipc` that
collided with the LaunchServer service's generated header; replaced by building the
LaunchServer service, which emits the IPC headers into a global include dir).
CI note: the first real CI runs (Ubuntu noble, Clang 18) hit a compiler-version
gap vs local dev (Arch, Clang 22) — two errors in AK (`-Winvalid-constexpr` on an
unusable `constexpr` in `Time.h`; undefined `__GCC_DESTRUCTIVE_SIZE` from
`Platform.h`). Resolved by **pinning CI to Clang 22** via apt.llvm.org in
`.github/workflows/ci.yml` (matches dev at 22.1.8), with patch 0011 kept as
defense-in-depth for the `constexpr` case. CI is now green on the pinned toolchain,
so M0–M2 are complete. **M3 is also complete** — a real X11 display
(`X11ScreenBackend`, dirty-rect `XPutImage` blit with optional zero-copy/`XShm`) and
real input (a pump thread + self-pipe notifier delivering mouse/keyboard to
`ScreenInput`) wired into WindowServer via a new `Mode=X11` screen mode (patch 0012;
third `ScreenBackend`, no compositor changes). All Xlib stays in SerenaDE's
`src/WindowServerX11`; the Serenity tree only gains a forward-declaring hook header
(`SerenadeX11.h`) plus the mode plumbing. Verified headless against a 24-bit Xvfb
 (render, cursor motion, button press/drag/release, and typed keys). **M4 exit criteria
  are met:** Terminal and FileManager build via Lagom and render real content headlessly
   (patches 0013–0018): Terminal behind a deterministic golden (`m4-terminal-echo`), and
   FileManager behind a functional `--expect-window` check (`m4-filemanager-docs`) since its
   window carries host-dependent content that an exact golden can't match across hosts;
   cross-app copy/paste is proven by two fixture processes over the
  real Clipboard service (`m4-clipboard-cross-app`); and launch-from-desktop is proven
  functionally via a `launch-terminal` fixture that asks LaunchServer to spawn Terminal
  (the desktop menu's own IPC path), asserted with the launcher's new `--expect-window`
   check since an interactive terminal can't be goldened (`m4-launch-terminal`). The
   launcher also gained `--home <dir>` and `--co-app <binary>`. The literal Taskbar/desktop
   UI is now built too (patches 0019–0022): the real Serenity Taskbar compiles under Lagom
   (its one heavy `<WindowServer/Window.h>` include is swapped for a new light
   `WMEventMask.h`), desktop app discovery honours `$SERENADE_APP_DIR` on hosts so the dock
   can list apps whose executables actually exist, and `Core::Process::spawn` supports a
   working directory off-Serenity via the portable `..._np` chdir action. Patch 0022 guards
   `AppletManager::set_position` against a null window — the Taskbar is a WM client with no
   applets, and its disconnect used to crash WindowServer (timing-dependent: post-screenshot
   locally, pre-screenshot on CI). A scripted click
   on the Terminal quick-launch dock icon launches the built Terminal through the Taskbar's
     own spawn path (`m4-taskbar-launch`, functional `--expect-window`). The **Settings** app
     also builds and renders on host now (patch 0023 — it links only already-built Lagom libs,
     so no portability changes); it shows its panel grid deterministically from the pinned
     `/res/apps`, covered by a golden test (`m4-settings`). The **ImageViewer** app builds and
      renders on host too (patch 0024 — wired `LibFileSystemAccessClient` + its generated IPC
      endpoint headers into Lagom, added after the Services subdir since it depends on the
      WindowServer target); launched with no file argument it shows its empty window deterministically
      without needing a running FileSystemAccessServer, covered by a golden test (`m4-imageviewer`).
      The **PixelPaint** app (the last M4 app-subset item) builds and renders on host as well
      (patch 0025 — added to the apps list with its GML include path; building it surfaced a latent
      out-of-bounds write in `BrushTool::build_cursor` for small brushes, fixed by flooring the cursor
      box); launched with no file argument it shows its empty document deterministically, covered by a
      golden test (`m4-pixelpaint`, fresh per-run `$HOME` since it restores/saves window placement).
       All five M4 apps (Terminal, FileManager, Settings, ImageViewer, PixelPaint) now build and render.
       Full serial ctest is **250/250**. See `docs/PORTING.md`.

**M5 — complete (2026-09-16; BSD matrix moved to M7).** FreeBSD support + polish. A portability audit of the shim
(`src/`) found it already BSD-clean (only X11/XShm, both portable; no evdev/epoll//proc). Patches 0026–0035
then cleared every compile/link blocker on a real FreeBSD VM: glibc-only `posix_spawn` features gated to
Serenity/glibc with portable fallbacks (0026 Process, 0027 FileManager), LLD auto-selection skipped (0028),
portable `<sys/sysmacros.h>` in gpu.h (0029), `prctl` skipped on BSDs in CrashTest + test262-runner
(0030–0031), `environ` declared in FileManager (0032), Terminal's `forkpty` declared directly (0033) and
libutil linked (0035), and explicit signal/wait includes in the Kernel wait tests (0034). The full build +
link now succeeds on FreeBSD, and C++26 compiles under the VM's clang 19 — the earlier "needs Clang 22" risk
did not materialize. The `build-freebsd` CI job boots a real FreeBSD VM via **vmactions/freebsd-vm** on a
hosted `ubuntu-latest` runner (no self-hosted machine needed) and fetches the pinned source in-VM. The first
*runtime* failure was WindowServer dying at startup: LibCore had no *BSD FileWatcher backend, so
`MUST(Core::FileWatcher::create())` in its EventLoop (and in Taskbar/ConfigServer/etc.) hit the unimplemented
stub's ENOTSUP; patch 0036 adds an inert *BSD backend (`FileWatcherBsd.cpp`) so create() always succeeds.
The last failures were two root-only permission tests (`TestSFTPStat::no_permission`,
`TestSqlDatabase::create_from_unreadable_file`); running the suite as a non-root user hung a GUI test, so
patch 0037 keeps ctest as root and skips those cases when `getuid() == 0`. The CI job pushes full ctest
output to a `ci-diagnostics` branch on failure for diagnosis. The polish items are done:
`--scale` (26bd2a9, verified 2x physical-pixel ratio) and `--cursor-theme` (9ac0cec, verified render +
selection); font coverage and WM focus/raise were verified working against the existing golden tests.
The remaining M5 item — full-suite green in the FreeBSD VM — plus NetBSD and
OpenBSD support are now M7's subtasks (the FreeBSD CI job, removed in 7ebcda8
while Linux was the focus, comes back as M7's first task).

**M6 — complete (2026-09-16).** Heavy apps. Both exit criteria met: audio plays in
Piano, and Browser loads web pages (`m6-browser-load`). First, an architecture finding that shrank the scope:
this Serenity version needs **no NetworkServer or SystemServer shim** — `Core::Socket` uses raw BSD
sockets (`socket()`/`connect()`, DNS via `getaddrinfo()`) and process spawning is direct
`posix_spawn` (`Core::Process::spawn`, `IPCProcess::spawn_and_connect_to_process`). The "server"
services are not in the data path at all. Two real work items remained: host audio, and Lagom patches
for the Browser binaries.

**Audio (done).** AudioServer now routes its mixed output through PulseAudio on hosts (patch 0038 —
a `pa_simple` S16LE-stereo sink created in `Mixer`, `write_to_device()` backend split by
`AK_OS_SERENITY`; also stops the mixer thread in `~Mixer`, which previously blocked forever in
`Thread::join()` on error unwinding). It builds on host (0039); `LibAudio`'s `ConnectionToServer` —
the client path apps like Piano actually use — now compiles on hosts too, with a `THREAD_PRIORITY_MAX`
fallback where the kernel header lacks it (0040). Lagom gained LibDSP + Piano (0041; Piano needed the
binary-dir include path for its GML headers, same pattern as PixelPaint/FileManager). Building Piano
surfaced a real bug: it calls `Thread::set_priority()` *before* `start()`, and glibc dereferences the
default-constructed `pthread_t` as a TCB pointer → segfault; patch 0042 defers the priority to just
after `pthread_create`. The launcher now groups `--service` entries by binary (AudioServer owns both
the `audio` and `audiomanager` sockets, exactly like SystemServer hands them to one process).
Verification is functional, not golden: `m6-piano-audio` (`tests/scripts/piano-audio.sh`) brings up the
full session, holds a keyboard key 3s via scripted input, records the default sink's `.monitor` source
with `parec` for the whole run, and asserts ≥2 non-silent seconds — i.e. click → Piano DSP → AudioServer
IPC (including the shared-buffer fd transfer over SCM_RIGHTS) → PulseAudio all worked. Skips (77) on
hosts without a usable PulseAudio.

**Browser (done).** Patches 0043–0048 build the rest of the stack: ImageDecoder/RequestServer/
SQLServer/WebContent as always-built host services (0043), with their service-side `compile_ipc`
skipped in Lagom because the client libs already generate the same endpoint headers (0044);
no-op'd jail-mode entry points that Browser calls unconditionally before exec (0045); the
out-of-process web view (0046); and Browser + BrowserSettings in the Lagom app list, with LibWebView
moved after LibFileSystemAccessClient (0047) plus its GML include path (0048). The launcher gained a
`--broker-service <socket>=<binary>` mode: those services start from an *accepted* client socket on fd 3
(one process per connection, as SystemServer hands them out), so the launcher pre-binds the listener and
accept/spawns per connection; teardown is bounded (`kill_and_reap` escalates TERM→KILL after 5s). Two
environment facts: Browser needs a seeded `$HOME` (it reads `~/.config/BrowserContentFilters.txt`
unconditionally — seed from `Base/home/anon`) and a private `XDG_RUNTIME_DIR` (stale
`Ladybird.{pid,socket}` singleton state otherwise makes the new process exit). Verification is
functional: `m6-browser-load` (`tests/scripts/browser-load.sh`) serves a marker page locally, asserts the
GET in the server log, and pixel-checks the screenshot (red block + rendered text; stdlib PNG decode that
handles content-based colortype 0/2/6). Patch 0049 fixed a latent bug the new build exposed: `Calendar.cpp`
loaded four fonts in static initializers, crashing any LibGUI-linking process without a resource root at
library-load time (first hit by `TestWebViewURL`, which Lagom registers once LibWeb is enabled) — the fonts
are now loaded on first use. Full serial ctest **262/262**; all 49 patches re-verified from a pristine pin.

**App completion (active, 2026-09-16).** With M6's exit criteria met, the remaining work is wiring
the rest of Serenity's apps onto the host session. Audit of the pin: 11 apps build via Lagom (About,
AnalogClock, Browser, BrowserSettings, Calculator, FileManager, ImageViewer, PixelPaint, Settings,
Piano, Terminal). Of the services still gated to SerenityOS, the one built apps actually need is
**FileSystemAccessServer** (file open/approval for ImageViewer/PixelPaint/TextEditor); NotificationServer
is deferred (no built app links LibNotificationClient yet). Plan, in order: (1) FileSystemAccessServer
on host — **done** (patch 0050: always-built + service-side `compile_ipc` guarded, same pattern as the
browser services; runs under the launcher as a broker service; `app-imageviewer-open` proves ImageViewer
opens a real image through the portal headless, since `request_file_read_only_approved` auto-approves);
(2) SoundPlayer — **done** (patches 0051–0052; `app-soundplayer-play` proves real playback via a .monitor
capture; 0052 also fixes a glibc EINVAL crash in the async_enqueue priority path that hit any app using
it); (3) Maps (deps built; tiles via RequestServer); (4) Mail (deps built incl. LibIMAP; needs the GML
binary-dir include path like Browser, patch 0048). TextEditor is deferred: it drags in LibMarkdown/
LibGemini/LibSyntax, which Lagom does not build yet.

## Patch workflow

See `patches/README.md`. Summary: create against the pinned ref in a scratch
clone, name `NNNN-short-description.patch`, keep it upstreamable in shape, and
verify `git apply --check` passes before pushing. Drop a patch from the same
commit that bumps the pin past its upstream merge.

## Upstreaming policy

| Change type                                                        | Goes to                |
| ------------------------------------------------------------------ | ---------------------- |
| Missing `AK_OS_*` guards in AK/LibCore/LibGfx/LibGUI               | `SerenityOS/serenity` PR |
| Lagom CMake support for more libraries (e.g. full LibGfx/LibGUI)   | `SerenityOS/serenity` PR |
| Generalization that upstream plausibly wants (backend plugin point)| `SerenityOS/serenity` PR |
| X11 screen/input backends, host service shims, launcher            | This repo              |
| SerenaDE-specific test harnesses, CI, resource env setup           | This repo              |

If a patch in `patches/` turns out to be wanted upstream, convert it to a PR;
do not let it linger here.

## CI

`.github/workflows/ci.yml`: Linux job checks out the pinned Serenity ref,
configures with `-DSERENITY_SOURCE_DIR`, builds everything, runs ctest. It must
stay green on every push. The FreeBSD job (self-hosted runner) mirrors it once
M5 starts. Any new third-party dependency requires an edit to this file and a
note in `docs/PORTING.md`.

## Working agreements for agents

- New source files carry the standard header:
  `Copyright (c) <year>, Gregory Norton.` + `SPDX-License-Identifier: BSD-2-Clause`.
- Keep the build green: stubs included, `cmake --build Build` must succeed.
- Update "Current status" (this file) and `docs/PORTING.md` with the work, not
  after it.
- Log non-obvious discoveries in the porting log (`docs/PORTING.md`) as you go.
- Never add a Linux-only code path without an explicit, named backend and a
  note in "Architecture decisions".
- Do not commit to this repo unless asked; do stage nothing that contains
  secrets or build artifacts.
