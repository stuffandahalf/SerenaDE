# serenade-launcher: bring up WindowServer + services headlessly, optionally
# replay scripted input, capture a screenshot, and compare it to a golden.

add_executable(serenade-launcher main.cpp)

# AK/, Kernel/API/ live at the Serenity root; LibCore/LibGfx headers under
# Userland/Libraries. The lagom build dirs carry generated headers.
target_include_directories(serenade-launcher PRIVATE
    ${SERENITY_SOURCE_DIR}
    ${SERENITY_SOURCE_DIR}/Userland
    ${SERENITY_SOURCE_DIR}/Userland/Libraries
    ${CMAKE_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/lagom
    ${CMAKE_BINARY_DIR}/lagom/Userland/Libraries
)

target_link_libraries(serenade-launcher PRIVATE AK LibCore LibGfx)
