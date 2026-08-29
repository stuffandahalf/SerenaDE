# The X11 side of the WindowServer port.
#
#   InputBackend.h           -- platform-neutral input seam (the interface)
#   X11ScreenBackend.*       -- ScreenBackend impl: X window + XShm, dirty-rect flush
#   X11InputBackend.*        -- X events -> KeyEvent / MousePacket
#   SyntheticInputBackend.h  -- scripted input for headless tests (M2)
#
# M0 status: nothing here is compiled yet. The backends need LibGfx/LibGUI to
# build natively first. When that lands, the backend sources are compiled into
# a static library that a tracked patch links into WindowServer, and
# serenade_input_backend (below) is consumed by tests/.

add_library(serenade_input_backend INTERFACE)
target_include_directories(serenade_input_backend INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
