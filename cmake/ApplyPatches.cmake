# Apply the tracked patch set to the Serenity source tree.
#
# Policy: patches/ holds the minimal set of in-tree modifications SerenaDE
# needs. Every patch must be small, reviewable, and plausibly upstreamable --
# the long-term goal is an empty directory. See patches/README.md.

file(GLOB SERENADE_PATCHES ${CMAKE_CURRENT_SOURCE_DIR}/../patches/*.patch)
list(SORT SERENADE_PATCHES)

foreach(patch IN LISTS SERENADE_PATCHES)
    get_filename_component(patch_name ${patch} NAME)

    execute_process(
        COMMAND git apply --check ${patch}
        WORKING_DIRECTORY ${SERENITY_SOURCE_DIR}
        RESULT_VARIABLE rc
    )
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR
            "Patch ${patch_name} does not apply cleanly to ${SERENITY_SOURCE_DIR}. "
            "Either the Serenity pin moved (bump SERENITY_PINNED_REF and rebase the patch) "
            "or the patch was upstreamed (drop it from patches/).")
    endif()

    message(STATUS "SerenaDE: applying ${patch_name}")
    execute_process(
        COMMAND git apply ${patch}
        WORKING_DIRECTORY ${SERENITY_SOURCE_DIR}
        RESULT_VARIABLE rc
    )
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${patch_name}")
    endif()
endforeach()
