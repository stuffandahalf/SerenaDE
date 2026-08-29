# Headless screenshot tests (M2).
#
# Run an app against WindowServer with the virtual screen backend plus
# SyntheticInputBackend, dump the framebuffer to PNG, and compare against
# golden/ images. This is the regression net for every later milestone and the
# reason M1/M2 exist before any X11 code lands.
#
# Not built until M1 (headless slice) prerequisites are in place.

# M2:
#   add_executable(screenshot-test screenshot_test.cpp)
#   target_link_libraries(screenshot-test PRIVATE serenade_input_backend ...)
#   add_test(NAME screenshot COMMAND screenshot-test)
