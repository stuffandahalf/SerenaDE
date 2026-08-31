# Apply the tracked patch set to the Serenity source tree.
#
# Policy: patches/ holds the minimal set of in-tree modifications SerenaDE
# needs. Every patch must be small, reviewable, and plausibly upstreamable --
# the long-term goal is an empty directory. See patches/README.md.

file(GLOB SERENADE_PATCHES ${CMAKE_CURRENT_SOURCE_DIR}/patches/*.patch)
list(SORT SERENADE_PATCHES)

foreach(patch IN LISTS SERENADE_PATCHES)
    get_filename_component(patch_name ${patch} NAME)

    execute_process(
        COMMAND git apply --check ${patch}
        WORKING_DIRECTORY ${SERENITY_SOURCE_DIR}
        RESULT_VARIABLE rc
    )
    if(rc EQUAL 0)
        message(STATUS "SerenaDE: applying ${patch_name}")
        execute_process(
            COMMAND git apply ${patch}
            WORKING_DIRECTORY ${SERENITY_SOURCE_DIR}
            RESULT_VARIABLE rc
        )
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "Failed to apply ${patch_name}")
        endif()
        continue()
    endif()

    # Not applicable forward: is it already applied (dev tree with manual edits)?
    execute_process(
        COMMAND git apply --check -R ${patch}
        WORKING_DIRECTORY ${SERENITY_SOURCE_DIR}
        RESULT_VARIABLE rc
    )
    if(rc EQUAL 0)
        message(STATUS "SerenaDE: ${patch_name} already applied, skipping")
        continue()
    endif()

    # Neither direction works. In a dev tree this is normal: the working copy
    # carries local edits beyond the tracked patch set (which also shifts the
    # context of earlier patches on the same file). CI is the strict gate --
    # it checks out the bare pin, where every patch must apply forward.
    message(WARNING
        "SerenaDE: ${patch_name} neither applies nor reverse-applies to "
        "${SERENITY_SOURCE_DIR}; continuing without it. This is expected while "
        "the dev tree carries edits beyond the tracked patches (regenerate the "
        "patch set before pushing). It is a real problem on a clean checkout of "
        "the pin: bump SERENITY_PINNED_REF and rebase, or drop an upstreamed patch.")
endforeach()
