# Flags required when compiling our own translation units against AK/Lagom
# headers (see Meta/Lagom/ReadMe.md, "Using Lagom in an External Project").
add_compile_options(-Wno-literal-suffix)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-fno-gnu-keywords)
endif()

add_subdirectory(WindowServerX11)
add_subdirectory(Services)
add_subdirectory(Launcher)
