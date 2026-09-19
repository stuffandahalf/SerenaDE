# Headless screenshot tests.
#
# M1: vertical slice -- WindowServer (virtual screen backend) + one real app,
# framebuffer dumped to PNG via SIGUSR1. Proves IPC over UDS (including fd
# passing via SOCKET_TAKEOVER), resource resolution ($SERENITY_RES), fonts,
# and the compositor end-to-end.
#
# M2: scripted input (FIFOs under a private input root) + golden-image
# comparison. The goldens in golden/ are the regression net for everything
# that lands after this milestone.

# The launcher uses fixed /tmp paths (/tmp/portal, /tmp/session/0/portal), so
# these tests must not run in parallel with anything else touching them; ctest
# is run serially for this project (see AGENTS.md).

# --- M1 --------------------------------------------------------------------

add_test(NAME m1-headless-analog-clock-screenshot
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/m1-analog-clock.png
        --delay 3000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:AnalogClock>)
set_tests_properties(m1-headless-analog-clock-screenshot PROPERTIES TIMEOUT 60)

# --- M2 --------------------------------------------------------------------

add_executable(screenshot-test screenshot_test.cpp)
target_include_directories(screenshot-test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Launcher
    ${SERENITY_SOURCE_DIR}
    ${SERENITY_SOURCE_DIR}/Userland
    ${SERENITY_SOURCE_DIR}/Userland/Libraries
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/lagom
    ${CMAKE_BINARY_DIR}/lagom/Userland/Libraries
)
target_link_libraries(screenshot-test PRIVATE AK LibCore LibGfx)

# Run one app with a scripted input sequence and compare the result against a
# golden image. The input root is private per test so FIFOs never collide.
# Optional 5th argument: ;-separated extra "socket=binary" services (e.g. the
# LaunchServer that Calculator talks to).
function(serenade_golden_test name app script golden)
    set(cmd_args
        serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/${name}.png
        --delay 2000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>)
    if(ARGV4)
        foreach(extra IN LISTS ARGV4)
            list(APPEND cmd_args --service ${extra})
        endforeach()
    endif()
    list(APPEND cmd_args
        --input-root ${CMAKE_CURRENT_BINARY_DIR}/input-${name}
        --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/${script}
        --golden ${CMAKE_CURRENT_SOURCE_DIR}/golden/${golden}
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:${app}>)
    add_test(NAME ${name} COMMAND ${cmd_args})
    set_tests_properties(${name} PROPERTIES TIMEOUT 90)
endfunction()

# M2 golden-screenshot tests. Three distinct apps are covered (Calculator,
# About, and the resize-test-window fixture), each with a deterministic golden:
#   - Calculator (real app): synthetic clicks compute 1 + 2 = 3 -> display "3".
#   - About (real app): idle render of the about dialog.
#   - resize-test-window (fixture): a normal, movable+resizable window used to
#     prove programmatic move (titlebar drag) and resize (border drag), since
#     the real apps either set_resizable(false) or are non-movable dialogs.
# AnalogClock is deliberately NOT a golden target: it renders the current wall
# clock, so its pixels are non-deterministic across runs.
serenade_golden_test(m2-calculator-clicks Calculator calculator-clicks.txt calculator-clicks.png
    "/tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>")
serenade_golden_test(m2-about About about-idle.txt about.png)
serenade_golden_test(m2-resize-test-move resize-test-window resize-move.txt resize-test-move.png)
serenade_golden_test(m2-resize-test-resize resize-test-window resize-resize.txt resize-test-resize.png)

# --- M4 --------------------------------------------------------------------
# Terminal runs a real shell on a pty and renders its output through LibVT. We
# drive it with -e (not the keyboard harness) so the content is deterministic:
# the command echoes a fixed line, then sleeps to hold the window open for the
# capture. A non-interactive `sh -c` prints no prompt and reads no rc files, so
# the golden is stable across hosts/shells. ConfigServer (Terminal reads config),
# LaunchServer (Help action + desktop-launch path) and Clipboard are started as
# services, exactly as the launcher would for a real session.
add_test(NAME m4-terminal-echo
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/m4-terminal.png
        --delay 7000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --golden ${CMAKE_CURRENT_SOURCE_DIR}/golden/terminal-echo.png
        $<TARGET_FILE:WindowServer>
        # `&&` (not `;`) keeps the shell alive for the capture without a CMake list
        # separator; a non-interactive `sh -c` prints no prompt, so output is fixed.
        $<TARGET_FILE:Terminal> -e "echo SERENADE_M4_OK && sleep 30")
set_tests_properties(m4-terminal-echo PROPERTIES TIMEOUT 90)

# Settings renders its icon grid of every app whose .af has Category=Settings. With no
# $SERENADE_APP_DIR override, discovery reads the pinned /res/apps (remapped to
# $SERENITY_RES by patch 0010), so the grid is a fixed set of panels -- deterministic and
# golden-able, like Calculator/About. ConfigServer (window/config state), LaunchServer and
# Clipboard run as services, as in a real session. No clicks: it renders on startup.
add_test(NAME m4-settings
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/m4-settings.png
        --delay 6000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --golden ${CMAKE_CURRENT_SOURCE_DIR}/golden/settings.png
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:Settings>)
set_tests_properties(m4-settings PROPERTIES TIMEOUT 90)

# ImageViewer launched with no file argument renders its empty window (title bar,
# full toolbar, blank view area) and never calls FileSystemAccessClient::the(), so
# no FileSystemAccessServer is needed at runtime -- only the standard config/launch/
# clipboard services. The render has no host-dependent content (no clock, directory
# listing, or timezone), so it is deterministic and golden-able like Settings.
add_test(NAME m4-imageviewer
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/m4-imageviewer.png
        --delay 6000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --golden ${CMAKE_CURRENT_SOURCE_DIR}/golden/imageviewer.png
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:ImageViewer>)
set_tests_properties(m4-imageviewer PROPERTIES TIMEOUT 90)

# PixelPaint launched with no file argument renders its default (empty) document -- toolbox,
# a transparent canvas, layer list, palette and statusbar -- and never calls
# FileSystemAccessClient::the(), so no FileSystemAccessServer is needed at runtime. The driver
# script gives it a fresh per-run $HOME because PixelPaint restores/saves its window
# size+position from config (keeping placement deterministic); the render has no host-dependent
# content, so it is golden-able like Settings/ImageViewer.
add_test(NAME m4-pixelpaint
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/pixelpaint-empty.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/m4-pixelpaint.png
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/pixelpaint.png
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:PixelPaint>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>)
set_tests_properties(m4-pixelpaint PROPERTIES TIMEOUT 90)

# FileManager opens a controlled Documents directory and renders its listing over
# the real host filesystem. The driver script (scripts/filemanager-docs.sh) builds
# that directory with known contents and pinned mtimes, then launches FileManager on
# it. The assertion is functional (--expect-window), not a pixel golden: FileManager's
# window includes host-dependent content (sidebar favorites, date-column timezone)
# that renders differently across hosts, so an exact golden is not portable. We assert
# a substantial directory window appeared instead. Config/Launch/Clipboard as services.
add_test(NAME m4-filemanager-docs
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/filemanager-docs.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/m4-filemanager.png
        ${CMAKE_CURRENT_BINARY_DIR}
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:FileManager>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>)
set_tests_properties(m4-filemanager-docs PROPERTIES TIMEOUT 90)

# Cross-app copy/paste (M4 exit criterion). clip-copy (the primary app) publishes a
# fixed string to the Clipboard IPC service; after --co-app-delay the launcher brings
# up clip-paste as a second process against the same WindowServer, which fetches and
# renders that string. If the round-trip works the paster shows "SERENADE_CLIP_OK";
# if not, "(empty)" -- so the golden is only stable when cross-app clipboard works.
# Both fixtures live in this repo (src/TestApps), so no Serenity patch is involved.
add_test(NAME m4-clipboard-cross-app
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/m4-clipboard.png
        --delay 4000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --co-app $<TARGET_FILE:clip-paste>
        --co-app-delay 2500
        --golden ${CMAKE_CURRENT_SOURCE_DIR}/golden/clipboard-cross-app.png
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:clip-copy>)
set_tests_properties(m4-clipboard-cross-app PROPERTIES TIMEOUT 90)

# Launch Terminal "from the desktop" (M4 exit criterion). launch-terminal is a
# stand-in for the desktop menu's "open this app": it asks LaunchServer to open the
# Terminal executable (Desktop::Launcher -> LaunchServer IPC -> Core::Process::spawn),
# exactly the path a real desktop click takes. The launched terminal is interactive
# (shell prompt + rc files), so its pixels are non-deterministic -- we can't golden it.
# Instead --expect-window asserts a window with content actually appeared (non-bg pixel
# fraction above the threshold); if the launch fails, no window shows and the test fails.
add_test(NAME m4-launch-terminal
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/m4-launch.png
        --delay 6000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --expect-window 0.05
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:launch-terminal> $<TARGET_FILE:Terminal>)
set_tests_properties(m4-launch-terminal PROPERTIES TIMEOUT 90)

# Launch Terminal from the real Taskbar UI (M4 exit criterion, full-fidelity). The driver
# script (scripts/taskbar-launch.sh) points desktop app discovery at a controlled apps dir
# holding one Terminal.af -> built binary, brings up the actual Taskbar, and scripts a click
# on the Terminal quick-launch dock icon. That drives Taskbar -> AppFile::spawn_with_escalation
# -> Core::Process::spawn, opening a real Terminal window on the same WindowServer. Functional
# (--expect-window), not a golden: the launched terminal is interactive/non-deterministic.
add_test(NAME m4-taskbar-launch
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/taskbar-launch.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/m4-taskbar.png
        ${CMAKE_CURRENT_BINARY_DIR}
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:Taskbar>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>
        $<TARGET_FILE:Terminal>)
set_tests_properties(m4-taskbar-launch PROPERTIES TIMEOUT 90)

# --- M6 --------------------------------------------------------------------
# Audio plays in a game (M6 exit criterion). The driver script brings up the
# real session (WindowServer + Piano, with ConfigServer/Clipboard/AudioServer
# as services), holds a keyboard key down for 3s via scripted input, and
# records the default PulseAudio sink's .monitor source for the whole run. It
# asserts the capture contains non-silent audio -- only possible if the full
# path works: click -> Piano DSP -> AudioServer IPC (shared-buffer fd transfer)
# -> PulseAudio. Skips when the host has no usable PulseAudio.
add_test(NAME m6-piano-audio
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/piano-audio.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/m6-piano.png
        ${CMAKE_CURRENT_BINARY_DIR}
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:Piano>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:AudioServer>)
# SKIP_RETURN_CODE is set explicitly: recent CTest no longer treats exit 77 as
# "Not Run" unless the property names it.
set_tests_properties(m6-piano-audio PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# Browser loads a web page (M6 exit criterion). The driver script brings up
# the real session (WindowServer + Browser, with ConfigServer/Clipboard/
# LaunchServer/SQLServer as services and WebContent/RequestServer/ImageDecoder
# as per-connection broker services), serves a marker page from a local
# python http.server, and asserts both that the server saw the page's GET
# with a 200 response and that the screenshot contains the page's red block
# plus text pixels -- only possible if the full path works: Browser ->
# WebContent (IPC) -> RequestServer (IPC) -> real HTTP fetch -> rendering.
# Skips when python3 is unavailable.
add_test(NAME m6-browser-load
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/browser-load.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/m6-browser.png
        ${CMAKE_CURRENT_BINARY_DIR}
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:Browser>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>
        $<TARGET_FILE:SQLServer>
        $<TARGET_FILE:WebContent>
        $<TARGET_FILE:RequestServer>
        $<TARGET_FILE:ImageDecoder>)
set_tests_properties(m6-browser-load PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# ImageViewer opens a real file (app-completion: the first services from the
# SerenityOS-only set running on host -- FileSystemAccessServer for the open,
# ImageDecoder for the decode). The CLI file argument goes through
# request_file_read_only_approved(), which auto-approves, so this is headless:
# the assertion is that the generated red test image is visible in the
# screenshot. Skips when python3 is unavailable.
add_test(NAME app-imageviewer-open
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/imageviewer-open.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/app-imageviewer.png
        ${CMAKE_CURRENT_BINARY_DIR}
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:ImageViewer>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>
        $<TARGET_FILE:FileSystemAccessServer>
        $<TARGET_FILE:ImageDecoder>)
set_tests_properties(app-imageviewer-open PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# SoundPlayer plays a real file (app-completion: the first app of the plan
# whose only new requirement is already-built services -- AudioServer for the
# playback, ImageDecoder because the app connects to it at startup). A
# generated sine WAV is passed as the CLI argument; the assertion is that the
# default sink's .monitor capture contains audible audio (parec), i.e. the
# full path worked: SoundPlayer -> LibAudio loader -> AudioServer IPC ->
# PulseAudio. Skips when no usable PulseAudio is present.
add_test(NAME app-soundplayer-play
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/soundplayer-play.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/app-soundplayer.png
        ${CMAKE_CURRENT_BINARY_DIR}
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:SoundPlayer>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>
        $<TARGET_FILE:AudioServer>
        $<TARGET_FILE:ImageDecoder>)
set_tests_properties(app-soundplayer-play PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# Maps renders its window (app-completion: third app of the plan). All link
# deps already build (LibMaps with FileManager, LibProtocol with the browser
# stack); patch 0053 adds it to the Lagom app list plus the GML binary-dir
# include path. RequestServer must run as a broker service -- it takes over an
# *accepted* client socket (one process per connection), so a regular
# --service entry would make it read from its own listener and crash in
# recvmsg with EINVAL. Tiles load over HTTPS through RequestServer when the
# host has network + CA certificates (the script seeds $HOME/.config/certs.pem
# best-effort); otherwise the map area shows its fallback text, so the check
# is functional (--expect-window), not a golden.
add_test(NAME app-maps-render
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/maps-render.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/app-maps.png
        ${CMAKE_CURRENT_BINARY_DIR}
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:Maps>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>
        $<TARGET_FILE:RequestServer>)
set_tests_properties(app-maps-render PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# Mail renders its window (app-completion: fourth app of the plan). All link
# deps already build (LibIMAP is a standard Lagom library, LibWebView/LibWeb
# with the browser stack); patch 0054 adds it to the Lagom app list plus the
# GML binary-dir include path. WebContent must run as a broker service: Mail's
# out-of-process web view connects at construction and aborts if the service
# is missing. With no account configured Mail shows its empty inbox UI -- no
# IMAP server or network needed. The check is functional (--expect-window),
# not a golden, since the web view area is rendered by the WebContent process.
add_test(NAME app-mail-render
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mail-render.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/app-mail.png
        ${CMAKE_CURRENT_BINARY_DIR}
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:Mail>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>
        $<TARGET_FILE:WebContent>)
set_tests_properties(app-mail-render PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# TextEditor opens a real file (app-completion: the last app of the plan).
# Patches 0055–0056 make it build: LibCMake joins the standard Lagom libraries
# (TextEditor's CMake highlighting), and LibShell's SyntaxHighlighter.cpp --
# whose vtable TextEditor references -- is no longer gated to SerenityOS. The
# CLI file argument goes through request_file_read_only_approved(), which
# auto-approves, so this is headless: the assertion is that the fixed test
# document is visible in the screenshot (a golden), i.e. TextEditor ->
# FileSystemAccessServer IPC -> open -> render all worked. The markdown/HTML
# preview web view is created lazily and a .txt triggers none, so no
# WebContent service is needed. Fresh per-run $HOME keeps the window placement
# deterministic; the blinking cursor can flip between runs but stays far below
# the compare tolerance.
add_test(NAME app-texteditor-open
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/texteditor-open.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/app-texteditor.png
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/texteditor-open.png
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:TextEditor>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>
        $<TARGET_FILE:FileSystemAccessServer>)
set_tests_properties(app-texteditor-open PROPERTIES TIMEOUT 90)

# --- Second Lagom batch ------------------------------------------------------
# The remaining host-buildable Serenity apps, added to the Lagom app list by
# patch 0057 (plus per-app portability patches). Six render deterministic
# windows with no file argument and are golden-checked through the shared
# app-golden.sh driver (fresh per-run $HOME keeps placement stable); the rest
# show host-dependent content (dates, network interfaces, monitor info) or a
# lazily-populated view, so they get a functional --expect-window check.

# CharacterMap renders its Unicode grid from the pinned LibUnicode data: no
# host-dependent content, golden-able.
add_test(NAME app-charactermap
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/app-golden.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/app-charactermap.png
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/charactermap.png
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:CharacterMap>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>)
set_tests_properties(app-charactermap PROPERTIES TIMEOUT 90)

# Run lists the apps from the pinned /res/apps .af files; the list and icons
# are fixed, so the render is golden-able.
add_test(NAME app-run
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/app-golden.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/app-run.png
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/run.png
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:Run>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>)
set_tests_properties(app-run PROPERTIES TIMEOUT 90)

# GamesSettings renders the card-game and chess settings panels, including the
# card back icon view over /res/graphics/cards and the chess piece previews --
# the first host render of LibCards/LibChess. All content comes from pinned
# res files and defaults, so it is golden-able.
add_test(NAME app-gamessettings
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/app-golden.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/app-gamessettings.png
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/gamessettings.png
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:GamesSettings>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>)
set_tests_properties(app-gamessettings PROPERTIES TIMEOUT 90)

# ThemeEditor renders the default theme's window preview and property panels;
# everything derives from the pinned res theme, so it is golden-able.
add_test(NAME app-themeeditor
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/app-golden.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/app-themeeditor.png
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/themeeditor.png
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:ThemeEditor>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>)
set_tests_properties(app-themeeditor PROPERTIES TIMEOUT 90)

# VideoPlayer with no file argument renders its empty player (transport
# controls, blank video area); deterministic and golden-able.
add_test(NAME app-videoplayer
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/app-golden.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/app-videoplayer.png
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/videoplayer.png
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:VideoPlayer>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>)
set_tests_properties(app-videoplayer PROPERTIES TIMEOUT 90)

# HexEditor with no file argument renders its empty editor (toolbar, blank hex
# area). Opening a file goes through the interactive request_file() prompt, so
# the headless test uses the empty state; deterministic and golden-able.
add_test(NAME app-hexeditor
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/app-golden.sh
        $<TARGET_FILE:serenade-launcher>
        ${SERENITY_SOURCE_DIR}/Base/res
        ${CMAKE_CURRENT_BINARY_DIR}/app-hexeditor.png
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/golden/hexeditor.png
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:HexEditor>
        $<TARGET_FILE:ConfigServer>
        $<TARGET_FILE:Clipboard>
        $<TARGET_FILE:LaunchServer>)
set_tests_properties(app-hexeditor PROPERTIES TIMEOUT 90)

# Calendar renders the current month's grid: the dates are wall-clock dependent,
# so a functional window check instead of a golden.
add_test(NAME app-calendar
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app-calendar.png
        --delay 5000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --expect-window 0.05
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:Calendar>)
set_tests_properties(app-calendar PROPERTIES TIMEOUT 90)

# CalendarSettings renders its settings panels with default values; the panel
# content is fixed but the check stays functional (window presence) since no
# golden was captured for it.
add_test(NAME app-calendarsettings
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app-calendarsettings.png
        --delay 5000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --expect-window 0.05
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:CalendarSettings>)
set_tests_properties(app-calendarsettings PROPERTIES TIMEOUT 90)

# CertificateSettings with a fresh (per-CI-job) config shows its empty
# certificate store UI; functional window check.
add_test(NAME app-certificatesettings
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app-certificatesettings.png
        --delay 5000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --expect-window 0.05
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:CertificateSettings>)
set_tests_properties(app-certificatesettings PROPERTIES TIMEOUT 90)

# DisplaySettings renders its settings panels; the monitor section reflects the
# WindowServer's screen layout (virtual screen in CI), so a functional check.
add_test(NAME app-displaysettings
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app-displaysettings.png
        --delay 5000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --expect-window 0.05
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:DisplaySettings>)
set_tests_properties(app-displaysettings PROPERTIES TIMEOUT 90)

# FontEditor connects to the FileSystemAccessClient at startup (its font-open
# path), so the FileSystemAccessServer must run as a broker service. With no
# file argument it renders its empty editor; functional window check.
add_test(NAME app-fonteditor
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app-fonteditor.png
        --delay 5000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --broker-service /tmp/session/0/portal/filesystemaccess=$<TARGET_FILE:FileSystemAccessServer>
        --expect-window 0.05
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:FontEditor>)
set_tests_properties(app-fonteditor PROPERTIES TIMEOUT 90)

# KeyboardMapper loads the system keymap; host builds have no kernel keymap, so
# it falls back to the default en-us map from /res/keymaps (patch 0058). The
# rendered keyboard is fixed content; functional window check.
add_test(NAME app-keyboardmapper
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app-keyboardmapper.png
        --delay 5000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --expect-window 0.05
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:KeyboardMapper>)
set_tests_properties(app-keyboardmapper PROPERTIES TIMEOUT 90)

# KeyboardSettings renders its settings panels with defaults; functional check.
add_test(NAME app-keyboardsettings
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app-keyboardsettings.png
        --delay 5000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --expect-window 0.05
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:KeyboardSettings>)
set_tests_properties(app-keyboardsettings PROPERTIES TIMEOUT 90)

# MailSettings shows the (empty) account list; functional check.
add_test(NAME app-mailsettings
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app-mailsettings.png
        --delay 5000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --expect-window 0.05
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:MailSettings>)
set_tests_properties(app-mailsettings PROPERTIES TIMEOUT 90)

# MapsSettings renders the map settings (tile server, default location);
# functional check.
add_test(NAME app-mapssettings
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app-mapssettings.png
        --delay 5000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --expect-window 0.05
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:MapsSettings>)
set_tests_properties(app-mapssettings PROPERTIES TIMEOUT 90)

# MouseSettings renders its sliders/spinboxes with default values; functional
# check. (It includes <WindowServer/Screen.h> for the acceleration limits, so
# patch 0057 also points it at the host gpu.h shim.)
add_test(NAME app-mousesettings
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app-mousesettings.png
        --delay 5000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --expect-window 0.05
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:MouseSettings>)
set_tests_properties(app-mousesettings PROPERTIES TIMEOUT 90)

# Screenshot captures the screen on launch and shows it in its window; the
# captured content includes whatever is on screen, so a functional check.
add_test(NAME app-screenshot
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app-screenshot.png
        --delay 5000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --expect-window 0.05
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:Screenshot>)
set_tests_properties(app-screenshot PROPERTIES TIMEOUT 90)

# TerminalSettings renders its settings panels with defaults; functional check.
add_test(NAME app-terminalsettings
    COMMAND serenade-launcher
        --res ${SERENITY_SOURCE_DIR}/Base/res
        --screenshot ${CMAKE_CURRENT_BINARY_DIR}/app-terminalsettings.png
        --delay 5000
        --log-dir ${CMAKE_CURRENT_BINARY_DIR}
        --service /tmp/session/0/portal/config=$<TARGET_FILE:ConfigServer>
        --service /tmp/session/0/portal/clipboard=$<TARGET_FILE:Clipboard>
        --service /tmp/session/0/portal/launch=$<TARGET_FILE:LaunchServer>
        --expect-window 0.05
        $<TARGET_FILE:WindowServer>
        $<TARGET_FILE:TerminalSettings>)
set_tests_properties(app-terminalsettings PROPERTIES TIMEOUT 90)

