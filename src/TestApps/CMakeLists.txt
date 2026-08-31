# Test fixture apps built by SerenaDE itself (not ported Serenity apps).
# These exist to give the golden-screenshot tests deterministic targets for
# specific input behaviors.

add_executable(resize-test-window resize_test_window.cpp)

# AK/, Kernel/API/ live at the Serenity root; LibCore/LibGUI headers under
# Userland/Libraries. The lagom build dirs carry generated headers.
target_include_directories(resize-test-window PRIVATE
    ${SERENITY_SOURCE_DIR}
    ${SERENITY_SOURCE_DIR}/Userland
    ${SERENITY_SOURCE_DIR}/Userland/Libraries
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/lagom
    ${CMAKE_BINARY_DIR}/lagom/Userland/Libraries
    ${CMAKE_BINARY_DIR}/lagom/Userland/Services
)

target_link_libraries(resize-test-window PRIVATE AK LibCore LibGfx LibGUI LibMain)
