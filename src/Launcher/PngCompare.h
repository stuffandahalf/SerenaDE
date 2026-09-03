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

// Fraction of pixels that differ from the dominant (background) color by more than
// `fuzz` per channel. Used to assert "a window with content appeared" without
// exact-pixel matching, for targets whose content is non-deterministic (e.g. an
// interactive terminal launched through LaunchServer). The background is found by
// quantizing each channel to its top 4 bits and taking the most frequent bucket.
double non_background_fraction(Gfx::Bitmap const& bmp, int fuzz)
{
    auto w = static_cast<int>(bmp.width());
    auto h = static_cast<int>(bmp.height());

    size_t counts[16 * 16 * 16] = {};
    for (int y = 0; y < h; ++y) {
        auto const* p = bmp.scanline_u8(y);
        for (int x = 0; x < w; ++x) {
            size_t idx = ((p[x * 4 + 0] >> 4) << 8) | ((p[x * 4 + 1] >> 4) << 4) | (p[x * 4 + 2] >> 4);
            counts[idx]++;
        }
    }

    size_t best = 0;
    int br = 0, bg = 0, bb = 0;
    for (int r = 0; r < 16; ++r)
        for (int g = 0; g < 16; ++g)
            for (int b = 0; b < 16; ++b) {
                size_t idx = (static_cast<size_t>(r) << 8) | (static_cast<size_t>(g) << 4) | static_cast<size_t>(b);
                if (counts[idx] > best) {
                    best = counts[idx];
                    br = r * 17; // bucket midpoint
                    bg = g * 17;
                    bb = b * 17;
                }
            }

    size_t nonbg = 0;
    for (int y = 0; y < h; ++y) {
        auto const* p = bmp.scanline_u8(y);
        for (int x = 0; x < w; ++x) {
            int d0 = std::abs(static_cast<int>(p[x * 4 + 0]) - br);
            int d1 = std::abs(static_cast<int>(p[x * 4 + 1]) - bg);
            int d2 = std::abs(static_cast<int>(p[x * 4 + 2]) - bb);
            int d = d0 > d1 ? (d0 > d2 ? d0 : d2) : (d1 > d2 ? d1 : d2);
            if (d > fuzz)
                ++nonbg;
        }
    }
    return static_cast<double>(nonbg) / (static_cast<double>(w) * h);
}

}
