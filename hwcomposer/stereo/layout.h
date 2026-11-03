// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <cstdint>

namespace waydroid::stereo {
// Private input vocabulary. Values are NOT Matroska or AVC wire values.
// See README.md for specification mappings and unavailable-input diagnostics.
enum class Layout : int32_t {
    Unset = 0, Mono = 1,
    SideBySideLeft = 2, SideBySideRight = 3,
    TopBottomLeft = 4, TopBottomRight = 5,
    RowsLeft = 6, RowsRight = 7,
    ColumnsLeft = 8, ColumnsRight = 9,
    CheckerboardLeft = 10, CheckerboardRight = 11,
    TemporalLeft = 12, TemporalRight = 13,
    TilesLeft = 14, TilesRight = 15,
    SeparateLeft = 16, SeparateRight = 17,
    AnaglyphCyanRed = 18, AnaglyphGreenMagenta = 19,
    UnspecifiedViews = 20,
    SideBySideQuincunxLeft = 21, SideBySideQuincunxRight = 22,
};
struct Size { uint32_t width, height; };
// Output dimensions include both eyes. No interpolation or upconversion.
const char* output_size(Layout layout, Size input, Size* output);
// Byte copies preserve each source sample. Input stride and allocation length
// are explicit so padding and truncated mappings cannot be read as pixels.
bool repack(Layout layout, Size input, const uint8_t* source, size_t source_size,
            size_t stride, size_t sample_bytes, uint8_t* output, size_t output_bytes);
}
