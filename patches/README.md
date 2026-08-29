# patches/ — tracked modifications to the Serenity tree

This directory holds the **minimal** set of in-tree changes SerenaDE requires.
It is applied automatically at CMake configure time (`cmake/ApplyPatches.cmake`)
and must always apply cleanly — CI fails otherwise. The long-term goal is an
**empty directory**.

## Rules

1. Each patch is small, single-purpose, and written as if it could be sent to
   `SerenityOS/serenity` upstream (see AGENTS.md, "Upstreaming policy").
2. If a change is generic portability work (missing `AK_OS_*` guard, Lagom
   CMake support), it does **not** belong here — it belongs in an upstream PR.
3. When a patch is accepted upstream, drop it from this directory in the same
   commit that bumps the Serenity pin past the merge.

## Naming

`NNNN-short-description.patch`, ordered by application dependency, generated
with `git format-patch` style headers (author, date, message) so provenance is
visible:

```
0001-windowserver-add-x11-screen-backend-mode.patch
```

## Creating a patch

Work in a scratch clone of the pinned ref, make the change, then:

```sh
git format-patch -1 HEAD -o ~/git/SerenaDE/patches/   # rename to NNNN-*.patch
cmake --build Build                                    # must configure + build green
```

Verify locally with `git apply --check` against the pinned ref before pushing.
