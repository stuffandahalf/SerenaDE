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
