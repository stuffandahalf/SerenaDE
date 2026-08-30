# Headless screenshot tests.
#
# M1: vertical slice -- WindowServer (virtual screen backend) + one real app,
# framebuffer dumped to PNG via SIGUSR1. Proves IPC over UDS (including fd
# passing via SOCKET_TAKEOVER), resource resolution ($SERENITY_RES), fonts,
# and the compositor end-to-end.
#
# M2 (later): scripted input via SyntheticInputBackend + golden-image
# comparison; see screenshot_test.cpp.

# The launcher uses fixed /tmp paths (/tmp/portal, /tmp/session/0/portal), so
# this test must not run in parallel with anything else touching them; ctest
# is run serially for this project (see AGENTS.md).
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
