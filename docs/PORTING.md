# Porting status & log

Living tracker. Update in the same commit as the work it describes, and keep
"Current status" in AGENTS.md consistent with this file.

## Component status

| Component                          | Milestone | Status      | Notes |
| ---------------------------------- | --------- | ----------- | ----- |
| Serenity pin (FetchSerenity + CI)  | M0        | not started | Currently `main` placeholder in both places |
| Lagom build of LibGUI              | M0        | not started | Last core lib missing; LibGfx/LibIPC/etc. already native |
| ctest fully green                  | M0        | done*       | 237/237 serial; `-j` parallel is flaky (see log) — run serially |
| Headless vertical slice (1 app → PNG) | M1      | not started | Proves IPC/compositor/resources |
| Synthetic input + golden tests     | M2        | not started | Regression net for everything after |
| X11 screen backend                 | M3        | not started | XShm dirty-rect flush |
| X11 input backend                  | M3        | not started | Only real input backend (no evdev, by design) |
| ConfigServer / Clipboard           | M4        | partial     | ConfigServer already builds natively via Lagom; Clipboard not yet |
| SystemServer shim + LaunchServer   | M4        | not started | Keeps app code unmodified |
| Launcher + resource env            | M4        | not started | Placeholder binary exists, builds and runs |
| App subset (Terminal, FileManager, Settings, ImageViewer, PixelPaint) | M4 | not started | |
| FreeBSD support                    | M5        | not started | X-input path only; no evdev anywhere |
| NetworkServer / AudioServer shims  | M6        | not started | Unblocks Browser/Mail/games |

## Porting log

Record discoveries here as they happen (surprising `#ifdef` gaps, API quirks,
decisions with trade-offs). Newest first.

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
