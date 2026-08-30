# Fetch SerenityOS/serenity at a pinned revision when no local checkout is
# supplied via -DSERENITY_SOURCE_DIR.
#
# Pinning discipline: SERENITY_PINNED_REF must be a release tag or an explicit
# commit -- never a branch head -- so builds are reproducible. Bump it
# deliberately (own commit, patches rebased, CI green). See AGENTS.md.

include(FetchContent)

# Pinned revision (no active tags in the upstream repo; pinned by commit).
set(SERENITY_PINNED_REF "7784b1f535a431635443e04d37a9e279ddb9b8eb" CACHE STRING "Git ref of SerenityOS/serenity to build against")

FetchContent_Declare(
    serenity
    GIT_REPOSITORY https://github.com/SerenityOS/serenity.git
    GIT_TAG ${SERENITY_PINNED_REF}
)

FetchContent_GetProperties(serenity)
if(NOT serenity_POPULATED)
    message(STATUS "SerenaDE: fetching Serenity at ${SERENITY_PINNED_REF} (large repo; prefer -DSERENITY_SOURCE_DIR for development)")
    FetchContent_Populate(serenity)
endif()

set(SERENITY_SOURCE_DIR "${serenity_SOURCE_DIR}")
