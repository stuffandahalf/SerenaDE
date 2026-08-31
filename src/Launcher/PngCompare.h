/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// Pixel-wise comparison of two PNG files, for golden-screenshot tests.
// Used by serenade-launcher (--golden) and the standalone screenshot-test
// tool. Decoding goes through LibGfx so both sides see identical pixels.

#include <AK/Error.h>
#include <AK/StringView.h>
#include <LibCore/File.h>
#include <LibGfx/ImageFormats/ImageDecoder.h>
#include <cstdlib>

namespace Serenade {

struct PngCompareResult {
    bool sizes_match { false };
    int actual_width { 0 };
    int actual_height { 0 };
    int golden_width { 0 };
    int golden_height { 0 };
    size_t differing_pixels { 0 };
    int max_channel_delta { 0 };
};

ErrorOr<NonnullRefPtr<Gfx::Bitmap>> load_png_bitmap(StringView path)
{
    auto file = TRY(Core::File::open(path, Core::File::OpenMode::Read));
    auto data = TRY(file->read_until_eof());
    auto decoder_or_error = Gfx::ImageDecoder::try_create_for_raw_bytes(data);
    if (decoder_or_error.is_error())
        return decoder_or_error.release_error();
    auto& decoder = decoder_or_error.value();
    auto frame_or_error = decoder->frame(0);
    if (frame_or_error.is_error())
        return frame_or_error.release_error();
    auto bitmap = frame_or_error.value().image;
    if (!bitmap)
        return Error::from_string_literal("PNG frame has no image data");
    return bitmap.release_nonnull();
}

PngCompareResult compare_pngs(Gfx::Bitmap const& actual, Gfx::Bitmap const& golden, int tolerance)
{
    PngCompareResult result;
    result.actual_width = actual.width();
    result.actual_height = actual.height();
    result.golden_width = golden.width();
    result.golden_height = golden.height();
    if (actual.width() != golden.width() || actual.height() != golden.height())
        return result;
    result.sizes_match = true;

    for (int y = 0; y < actual.height(); ++y) {
        auto const* a = actual.scanline_u8(y);
        auto const* g = golden.scanline_u8(y);
        for (int x = 0; x < actual.width(); ++x) {
            int pixel_delta = 0;
            for (int channel = 0; channel < 4; ++channel) {
                int delta = std::abs(static_cast<int>(a[x * 4 + channel]) - static_cast<int>(g[x * 4 + channel]));
                if (delta > pixel_delta)
                    pixel_delta = delta;
            }
            if (pixel_delta > result.max_channel_delta)
                result.max_channel_delta = pixel_delta;
            if (pixel_delta > tolerance)
                ++result.differing_pixels;
        }
    }
    return result;
}

// True when the images match within tolerance: same size and at most
// max_diff_fraction of the pixels differ by more than `tolerance` per channel.
bool pngs_match_within(Gfx::Bitmap const& actual, Gfx::Bitmap const& golden, int tolerance, double max_diff_fraction)
{
    auto result = compare_pngs(actual, golden, tolerance);
    if (!result.sizes_match)
        return false;
    size_t total = static_cast<size_t>(actual.width()) * static_cast<size_t>(actual.height());
    return result.differing_pixels <= static_cast<size_t>(total * max_diff_fraction);
}

}
