# serenade-launcher: sets up the Serenity runtime environment on the host and
# starts WindowServer + services in dependency order (M4).
#
# Scaffold stage: builds a placeholder binary so CI has a concrete target; the
# real body lands with M4.

add_executable(serenade-launcher main.cpp)
