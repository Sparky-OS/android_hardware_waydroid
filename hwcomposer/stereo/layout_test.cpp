// SPDX-License-Identifier: Apache-2.0
#include "layout.h"
#include <gtest/gtest.h>
#include <array>
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
TEST(StereoLayout, SwappedEyeNegativeControlIsDetectable) {
    std::array<uint8_t,8> out{};
    ASSERT_TRUE(repack(Layout::SideBySideRight, {4,2}, expected.data(), 8, 4, 1, out.data(), 8));
    EXPECT_NE(out, expected);
    EXPECT_EQ(out[0], 101); EXPECT_EQ(out[2], 1);
}
}
