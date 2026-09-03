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

# clip-copy / clip-paste: two separate GUI processes that share the Clipboard IPC
# service, for the M4 cross-app copy/paste golden test. They share the exact same
# include dirs and link set as resize-test-window.
foreach(clip_app IN ITEMS clip-copy clip-paste)
    string(REPLACE "-" "_" clip_src "${clip_app}")
    add_executable(${clip_app} ${clip_src}.cpp)
    target_include_directories(${clip_app} PRIVATE
        ${SERENITY_SOURCE_DIR}
        ${SERENITY_SOURCE_DIR}/Userland
        ${SERENITY_SOURCE_DIR}/Userland/Libraries
        ${CMAKE_BINARY_DIR}
        ${CMAKE_BINARY_DIR}/lagom
        ${CMAKE_BINARY_DIR}/lagom/Userland/Libraries
        ${CMAKE_BINARY_DIR}/lagom/Userland/Services
    )
    target_link_libraries(${clip_app} PRIVATE AK LibCore LibGfx LibGUI LibMain)
endforeach()
