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
    case Layout::TemporalLeft: case Layout::TemporalRight:
    case Layout::SeparateLeft: case Layout::SeparateRight:
        return "paired views require pair and frame identities; one buffer is insufficient";
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

bool repack(Layout layout, Size in, const uint8_t* src, size_t source_size,
            size_t stride, size_t bytes, uint8_t* dst, size_t output_bytes) {
    Size out{};
    if (output_size(layout, in, &out) || !src || !dst || !bytes || bytes > 16)
        return false;
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
            const uint32_t ex = x % eye_width;
            uint32_t sx = ex, sy = y;
            switch (layout) {
            case Layout::TopBottomLeft: case Layout::TopBottomRight:
                sy += eye * out.height; break;
            case Layout::RowsLeft: case Layout::RowsRight:
                sy = y * 2 + eye; break;
            case Layout::ColumnsLeft: case Layout::ColumnsRight:
                sx = ex * 2 + eye; break;
            case Layout::CheckerboardLeft: case Layout::CheckerboardRight:
                sx = ex * 2 + (eye ^ (y & 1)); break;
            case Layout::TilesLeft: case Layout::TilesRight:
                // AVC Annex D: R0 is frame 0. Frame 1 is R1 beside R2/R3.
                if (eye) {
                    const uint32_t third_w = in.width / 3, third_h = in.height / 3;
                    if (ex < third_w) sx = 2 * third_w + ex;
                    else { sx = ex - third_w + (y / third_h) * third_w;
                           sy = 2 * third_h + y % third_h; }
                }
                break;
            default: sx += eye * eye_width; break;
            }
            std::memcpy(dst + (size_t(y) * out.width + x) * bytes,
                        src + size_t(sy) * stride + size_t(sx) * bytes, bytes);
        }
    }
    return true;
}
}
