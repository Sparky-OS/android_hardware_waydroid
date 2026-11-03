// SPDX-License-Identifier: Apache-2.0
#include "layout.h"
#include <cstring>
#include <climits>
#include <limits>

namespace waydroid::stereo {
const char* output_size(Layout layout, Size in, Size* out) {
    if (!in.width || !in.height || in.width > INT_MAX || in.height > INT_MAX)
        return "dimensions exceed signed protocol coordinate range";
    *out = in;
    switch (layout) {
    case Layout::SideBySideLeft: case Layout::SideBySideRight:
    case Layout::ColumnsLeft: case Layout::ColumnsRight:
    case Layout::CheckerboardLeft: case Layout::CheckerboardRight:
    case Layout::SideBySideQuincunxLeft: case Layout::SideBySideQuincunxRight:
        return in.width % 2 ? "packing requires even width" : nullptr;
    case Layout::TopBottomLeft: case Layout::TopBottomRight:
    case Layout::RowsLeft: case Layout::RowsRight:
        if (in.width > INT_MAX / 2) return "repacked width exceeds signed protocol range";
        out->width *= 2;
        out->height /= 2;
        return in.height % 2 ? "packing requires even height" : nullptr;
    case Layout::TilesLeft: case Layout::TilesRight:
        if (in.width / 3 > INT_MAX / 4) return "repacked width exceeds signed protocol range";
        out->width = in.width / 3 * 4;
        out->height = in.height / 3 * 2;
        return in.width % 3 || in.height % 3 ? "tile packing requires multiples of three" : nullptr;
    case Layout::TemporalLeft:
        return "temporal left-first: producer must hand one side-by-side buffer, left first";
    case Layout::TemporalRight:
        return "temporal right-first: producer must hand one side-by-side buffer, left first";
    case Layout::SeparateLeft:
        return "separate views left-first: producer must hand one side-by-side buffer, left first";
    case Layout::SeparateRight:
        return "separate views right-first: producer must hand one side-by-side buffer, left first";
    case Layout::AnaglyphCyanRed: case Layout::AnaglyphGreenMagenta:
        return "anaglyph does not contain two recoverable full-colour views";
    case Layout::UnspecifiedViews:
        return "constituent view relationship is unspecified";
    case Layout::Unset: case Layout::Mono:
        return "not stereo";
    default:
        return "unknown stereo layout";
    }
}

const char* cropped_size(Layout layout, Size in, EyeCrop crop, uint32_t transform, Size* out) {
    Size full{};
    if (const char* reason = output_size(layout, in, &full)) return reason;
    if (transform & ~7u) return "unknown transform bits";
    if (!crop.width || !crop.height || crop.left >= full.width / 2 ||
        crop.top >= full.height || crop.width > full.width / 2 - crop.left ||
        crop.height > full.height - crop.top) return "per-eye crop outside native samples";
    const uint32_t width = transform & 4 ? crop.height : crop.width;
    const uint32_t height = transform & 4 ? crop.width : crop.height;
    if (width > INT_MAX / 2) return "transformed width exceeds signed protocol range";
    *out = {width * 2, height};
    return nullptr;
}

bool repack(Layout layout, Size in, const uint8_t* src, size_t source_size,
            size_t stride, size_t bytes, uint8_t* dst, size_t output_bytes) {
    Size full{};
    if (output_size(layout, in, &full)) return false;
    return repack_crop(layout, in, {0, 0, full.width / 2, full.height}, 0,
                       src, source_size, stride, bytes, dst, output_bytes);
}

bool repack_crop(Layout layout, Size in, EyeCrop crop, uint32_t transform,
                 const uint8_t* src, size_t source_size, size_t stride,
                 size_t bytes, uint8_t* dst, size_t output_bytes) {
    Size out{}, full{};
    if (output_size(layout, in, &full) || cropped_size(layout, in, crop, transform, &out) ||
        !src || !dst || !bytes || bytes > 16) return false;
    const size_t limit = std::numeric_limits<size_t>::max();
    if (in.width > limit / bytes || out.width > limit / bytes) return false;
    const size_t row_bytes = size_t(in.width) * bytes;
    const size_t out_stride = size_t(out.width) * bytes;
    if (stride < row_bytes || stride > (limit - row_bytes) / in.height ||
        source_size < stride * (in.height - 1) + row_bytes ||
        out.height > limit / out_stride || output_bytes < out_stride * out.height)
        return false;
    const uint32_t eye_width = out.width / 2;
    const bool reverse = layout == Layout::SideBySideRight || layout == Layout::TopBottomRight ||
            layout == Layout::RowsRight || layout == Layout::ColumnsRight ||
            layout == Layout::CheckerboardRight || layout == Layout::TilesRight ||
            layout == Layout::SideBySideQuincunxRight;
    for (uint32_t y = 0; y < out.height; ++y) {
        for (uint32_t x = 0; x < out.width; ++x) {
            const uint32_t eye = (x / eye_width) ^ reverse;
            // Invert the transform within each eye; never rotate the packed pair.
            uint32_t ex = x % eye_width, ey = y;
            if (transform & 4) { ex = y; ey = crop.height - 1 - x % eye_width; }
            if (transform & 1) ex = crop.width - 1 - ex;
            if (transform & 2) ey = crop.height - 1 - ey;
            ex += crop.left;
            ey += crop.top;
            uint32_t sx = ex, sy = ey;
            switch (layout) {
            case Layout::TopBottomLeft: case Layout::TopBottomRight:
                sy += eye * full.height; break;
            case Layout::RowsLeft: case Layout::RowsRight:
                sy = ey * 2 + eye; break;
            case Layout::ColumnsLeft: case Layout::ColumnsRight:
                sx = ex * 2 + eye; break;
            case Layout::CheckerboardLeft: case Layout::CheckerboardRight:
                sx = ex * 2 + (eye ^ (ey & 1)); break;
            case Layout::TilesLeft: case Layout::TilesRight:
                // AVC Annex D: R0 is frame 0. Frame 1 is R1 beside R2/R3.
                if (eye) {
                    const uint32_t third_w = in.width / 3, third_h = in.height / 3;
                    if (ex < third_w) sx = 2 * third_w + ex;
                    else { sx = ex - third_w + (ey / third_h) * third_w;
                           sy = 2 * third_h + ey % third_h; }
                }
                break;
            default: sx += eye * (full.width / 2); break;
            }
            std::memcpy(dst + (size_t(y) * out.width + x) * bytes,
                        src + size_t(sy) * stride + size_t(sx) * bytes, bytes);
        }
    }
    return true;
}
}
