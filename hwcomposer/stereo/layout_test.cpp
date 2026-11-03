// SPDX-License-Identifier: Apache-2.0
#include "layout.h"
#include <gtest/gtest.h>
#include <array>
#include <climits>
#include <limits>
#include <vector>
using namespace waydroid::stereo;
namespace {
const std::array<uint8_t, 8> expected = {1, 2, 101, 102, 3, 4, 103, 104};
void check(Layout layout, Size in, const std::vector<uint8_t>& pixels) {
    std::array<uint8_t, 10> out{};
    out.front() = out.back() = 255;
    ASSERT_TRUE(repack(layout, in, pixels.data(), pixels.size(), in.width, 1, out.data()+1, 8));
    EXPECT_EQ(out.front(), 255); EXPECT_EQ(out.back(), 255);
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), out.begin()+1));
}
TEST(StereoLayout, KnownAddressSamplesAllSpatialPackings) {
    check(Layout::SideBySideLeft, {4,2}, {1,2,101,102,3,4,103,104});
    check(Layout::SideBySideRight, {4,2}, {101,102,1,2,103,104,3,4});
    check(Layout::TopBottomLeft, {2,4}, {1,2,3,4,101,102,103,104});
    check(Layout::TopBottomRight, {2,4}, {101,102,103,104,1,2,3,4});
    check(Layout::RowsLeft, {2,4}, {1,2,101,102,3,4,103,104});
    check(Layout::RowsRight, {2,4}, {101,102,1,2,103,104,3,4});
    check(Layout::ColumnsLeft, {4,2}, {1,101,2,102,3,103,4,104});
    check(Layout::ColumnsRight, {4,2}, {101,1,102,2,103,3,104,4});
    check(Layout::CheckerboardLeft, {4,2}, {1,101,2,102,103,3,104,4});
    check(Layout::CheckerboardRight, {4,2}, {101,1,102,2,3,103,4,104});
    check(Layout::TilesLeft, {3,3}, {1,2,101,3,4,103,102,104,255});
    check(Layout::TilesRight, {3,3}, {101,102,1,103,104,3,2,4,255});
    check(Layout::SideBySideQuincunxLeft, {4,2}, {1,2,101,102,3,4,103,104});
    check(Layout::SideBySideQuincunxRight, {4,2}, {101,102,1,2,103,104,3,4});
}
TEST(StereoLayout, StrideAndBounds) {
    const uint8_t in[] = {1,2,101,102,255,255,3,4,103,104,255,255};
    uint8_t out[8]{};
    ASSERT_TRUE(repack(Layout::SideBySideLeft, {4,2}, in, sizeof(in), 6, 1, out, sizeof(out)));
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), out));
    EXPECT_FALSE(repack(Layout::SideBySideLeft, {4,2}, in, 9, 6, 1, out, sizeof(out)));
    EXPECT_FALSE(repack(Layout::SideBySideLeft, {4,2}, in, sizeof(in), 3, 1, out, sizeof(out)));
    EXPECT_FALSE(repack(Layout::SideBySideLeft, {4,2}, in, sizeof(in), 6, 1, out, 7));
}
TEST(StereoLayout, NativeResolutionAndPrecision) {
    Size out{};
    ASSERT_EQ(output_size(Layout::TopBottomLeft, {1920,1080}, &out), nullptr);
    EXPECT_EQ(out.width, 3840u); EXPECT_EQ(out.height, 540u);
    std::vector<uint64_t> input(1920*1080), output(input.size());
    for (size_t i=0; i<input.size(); ++i) input[i] = 0x1234567800000000ULL + i;
    ASSERT_TRUE(repack(Layout::TopBottomLeft, {1920,1080},
        reinterpret_cast<uint8_t*>(input.data()), input.size()*8, 1920*8, 8,
        reinterpret_cast<uint8_t*>(output.data()), output.size()*8));
    for (size_t y=0; y<540; ++y) {
        EXPECT_TRUE(std::equal(input.begin()+y*1920, input.begin()+(y+1)*1920, output.begin()+y*3840));
        EXPECT_TRUE(std::equal(input.begin()+(y+540)*1920, input.begin()+(y+541)*1920, output.begin()+y*3840+1920));
    }
}
TEST(StereoLayout, RefuseUnpairedUnknownAndBadGeometry) {
    Size out{};
    for (auto layout : {Layout::Unset, Layout::Mono, Layout::TemporalLeft, Layout::TemporalRight,
                       Layout::SeparateLeft, Layout::SeparateRight, Layout::AnaglyphCyanRed,
                       Layout::AnaglyphGreenMagenta, Layout::UnspecifiedViews, Layout(-1), Layout(999)})
        EXPECT_NE(output_size(layout, {4,2}, &out), nullptr);
    EXPECT_NE(output_size(Layout::SideBySideLeft, {3,2}, &out), nullptr);
    EXPECT_NE(output_size(Layout::TopBottomLeft, {4,3}, &out), nullptr);
    EXPECT_NE(output_size(Layout::TilesLeft, {4,3}, &out), nullptr);
    EXPECT_NE(output_size(Layout::SideBySideLeft, {0,2}, &out), nullptr);
}
TEST(StereoLayout, CheckedArithmeticWithoutArbitraryRasterLimit) {
    Size out{};
    ASSERT_EQ(output_size(Layout::TopBottomLeft, {20000,2}, &out), nullptr);
    EXPECT_EQ(out.width, 40000u); EXPECT_EQ(out.height, 1u);
    uint8_t sample = 0;
    EXPECT_FALSE(repack(Layout::TopBottomLeft, {uint32_t(INT_MAX/2), uint32_t(INT_MAX-1)},
                        &sample, 1, std::numeric_limits<size_t>::max(), 16, &sample, 1));
}
TEST(StereoLayout, SwappedEyeNegativeControlIsDetectable) {
    std::array<uint8_t,8> out{};
    ASSERT_TRUE(repack(Layout::SideBySideRight, {4,2}, expected.data(), 8, 4, 1, out.data(), 8));
    EXPECT_NE(out, expected);
    EXPECT_EQ(out[0], 101); EXPECT_EQ(out[2], 1);
}
}

TEST(StereoGeometry, EachEyeRotatesAndFlipsIndependently) {
    const uint8_t input[] = {1,2,3,101,102,103,4,5,6,104,105,106};
    // HWC flips precede its clockwise quarter-turn. These references are
    // hand-written 3x2/2x3 images, with the right eye offset by 100.
    const std::vector<std::vector<uint8_t>> left = {
        {1,2,3,4,5,6}, {3,2,1,6,5,4}, {4,5,6,1,2,3}, {6,5,4,3,2,1},
        {4,1,5,2,6,3}, {6,3,5,2,4,1}, {1,4,2,5,3,6}, {3,6,2,5,1,4}};
    for (uint32_t transform = 0; transform < 8; ++transform) {
        SCOPED_TRACE(transform);
        Size out{};
        ASSERT_EQ(cropped_size(Layout::SideBySideLeft, {6,2}, {0,0,3,2}, transform, &out), nullptr);
        const uint32_t width = transform & 4 ? 2 : 3;
        EXPECT_EQ(out.width, width * 2);
        uint8_t pixels[12]{};
        ASSERT_TRUE(repack_crop(Layout::SideBySideLeft, {6,2}, {0,0,3,2}, transform,
                                input, sizeof(input), 6, 1, pixels, sizeof(pixels)));
        for (unsigned i=0; i<6; ++i) {
            EXPECT_EQ(pixels[(i/width)*out.width+i%width], left[transform][i]);
            EXPECT_EQ(pixels[(i/width)*out.width+i%width+width], left[transform][i]+100);
        }
    }
}

TEST(StereoGeometry, CropPreservesPackingPhaseAndEyeIdentity) {
    const uint8_t columns[] = {1,101,2,102,3,103,4,104,5,105,6,106};
    const uint8_t checker[] = {1,101,2,102,3,103,104,4,105,5,106,6};
    const uint8_t rows[] = {1,2,3,101,102,103,4,5,6,104,105,106};
    const uint8_t tb[] = {1,2,3,4,5,6,101,102,103,104,105,106};
    const uint8_t expected[] = {5,6,105,106};
    struct Case { Layout layout; Size size; const uint8_t* pixels; };
    for (const auto& item : {Case{Layout::ColumnsLeft,{6,2},columns},
            Case{Layout::CheckerboardLeft,{6,2},checker},
            Case{Layout::RowsLeft,{3,4},rows}, Case{Layout::TopBottomLeft,{3,4},tb}}) {
        uint8_t out[4]{};
        ASSERT_TRUE(repack_crop(item.layout, item.size, {1,1,2,1}, 0, item.pixels,
                                12, item.size.width, 1, out, sizeof(out)));
        EXPECT_TRUE(std::equal(std::begin(expected), std::end(expected), out));
    }
}

TEST(StereoGeometry, Clip399KeepsOneHundredNativeColumns) {
    Size out{};
    ASSERT_EQ(cropped_size(Layout::TopBottomLeft, {192,216}, {0,0,100,108}, 0, &out), nullptr);
    EXPECT_EQ(out.width, 200u); EXPECT_EQ(out.height, 108u);
    EXPECT_NE(cropped_size(Layout::TopBottomLeft, {192,216}, {0,0,193,108}, 0, &out), nullptr);
    EXPECT_NE(cropped_size(Layout::TopBottomLeft, {192,216}, {0,0,100,108}, 8, &out), nullptr);
    EXPECT_NE(cropped_size(Layout::TopBottomLeft, {192,216}, {0,0,0,108}, 0, &out), nullptr);
}

TEST(StereoGeometry, FractionalEdgesFollowTheNativeEyeTransform) {
    // A 3x2 eye with an asymmetric subpixel box; expected corner positions
    // come from reflecting/rotating its four corners, not from raster copies.
    const EyeSource expected[] = {
        {.25,.5,1.5,1.25}, {1.25,.5,1.5,1.25}, {.25,.25,1.5,1.25}, {1.25,.25,1.5,1.25},
        {.25,.25,1.25,1.5}, {.25,1.25,1.25,1.5}, {.5,.25,1.25,1.5}, {.5,1.25,1.25,1.5}};
    for (uint32_t transform = 0; transform < 8; ++transform) {
        SCOPED_TRACE(transform);
        EyeSource out{};
        ASSERT_TRUE(transform_eye_source({.25,.5,1.5,1.25}, {3,2}, transform, &out));
        EXPECT_DOUBLE_EQ(out.left, expected[transform].left);
        EXPECT_DOUBLE_EQ(out.top, expected[transform].top);
        EXPECT_DOUBLE_EQ(out.width, expected[transform].width);
        EXPECT_DOUBLE_EQ(out.height, expected[transform].height);
    }
    EyeSource out{};
    EXPECT_FALSE(transform_eye_source({.25,.5,3,1.25}, {3,2}, 0, &out));
    EXPECT_FALSE(transform_eye_source({.25,.5,1.5,1.25}, {3,2}, 8, &out));
}
