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
