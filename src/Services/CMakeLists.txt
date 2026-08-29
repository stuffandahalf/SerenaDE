# Host shims for Serenity services that assume a Serenity kernel.
#
#   SystemServerShim/ -- speaks the same SystemServer IPC contract but launches
#                        real host processes via fork/exec (M4). Apps keep
#                        calling the normal IPC; nothing in app code changes.
#
# AudioServer / NetworkServer shims land in M6; until then the launcher simply
# does not start those services and apps that need them stay out of scope.

# M4: add_subdirectory(SystemServerShim)
