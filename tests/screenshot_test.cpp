/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// screenshot-test: compare an actual screenshot against a golden image.
//
// Usage:
//   screenshot-test --actual <png> --golden <png> [--tolerance <channel-delta>]
//
// Exit status 0 when the images match within tolerance, 1 otherwise. This is
// the standalone form of the comparison serenade-launcher performs with its
// --golden option; ctest uses either entry point.

#include "PngCompare.h"

#include <AK/Debug.h>
#include <AK/StringView.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv)
{
    const char* actual_path = nullptr;
    const char* golden_path = nullptr;
    int tolerance = 16;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--actual") && i + 1 < argc)
            actual_path = argv[++i];
        else if (!strcmp(argv[i], "--golden") && i + 1 < argc)
            golden_path = argv[++i];
        else if (!strcmp(argv[i], "--tolerance") && i + 1 < argc)
            tolerance = atoi(argv[++i]);
        else {
            fprintf(stderr, "Usage: %s --actual <png> --golden <png> [--tolerance <channel-delta>]\n", argv[0]);
            return 2;
        }
    }
    if (!actual_path || !golden_path) {
        fprintf(stderr, "Usage: %s --actual <png> --golden <png> [--tolerance <channel-delta>]\n", argv[0]);
        return 2;
    }

    auto actual_or_error = Serenade::load_png_bitmap({ actual_path, strlen(actual_path) });
    if (actual_or_error.is_error()) {
        auto message = actual_or_error.error().string_literal();
        fprintf(stderr, "screenshot-test: reading %s: %.*s\n",
            actual_path, static_cast<int>(message.length()), message.characters_without_null_termination());
        return 1;
    }
    auto golden_or_error = Serenade::load_png_bitmap({ golden_path, strlen(golden_path) });
    if (golden_or_error.is_error()) {
        auto message = golden_or_error.error().string_literal();
        fprintf(stderr, "screenshot-test: reading %s: %.*s\n",
            golden_path, static_cast<int>(message.length()), message.characters_without_null_termination());
        return 1;
    }

    auto const& actual = actual_or_error.value();
    auto const& golden = golden_or_error.value();
    auto result = Serenade::compare_pngs(actual, golden, tolerance);

    if (!result.sizes_match) {
        fprintf(stderr, "screenshot-test: FAIL size mismatch: actual %dx%d vs golden %dx%d\n",
            result.actual_width, result.actual_height, result.golden_width, result.golden_height);
        return 1;
    }

    size_t total = static_cast<size_t>(result.actual_width) * static_cast<size_t>(result.actual_height);
    printf("screenshot-test: %zu/%zu pixels differ (max delta %d, tolerance %d)\n",
        result.differing_pixels, total, result.max_channel_delta, tolerance);

    if (result.differing_pixels > total / 1000) {
        fprintf(stderr, "screenshot-test: FAIL more than 0.1%% of pixels differ\n");
        return 1;
    }
    printf("screenshot-test: PASS\n");
    return 0;
}
