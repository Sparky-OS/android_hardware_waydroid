// SPDX-License-Identifier: Apache-2.0
#include "composition.h"
#include <gtest/gtest.h>
using namespace waydroid::stereo;
namespace {
const CompositionLayer mono{}, stereo{true, false, false}, required{false, true, false};
TEST(StereoComposition, OrdinaryFrameIsUnchanged) {
    const auto plan = client_range({mono, mono, mono});
    EXPECT_EQ(plan.begin, 0u); EXPECT_EQ(plan.end, 3u);
    EXPECT_TRUE(plan.isolates_stereo);
}
TEST(StereoComposition, StereoBelowOrdinaryControls) {
    const auto plan = client_range({mono, stereo, mono, mono});
    EXPECT_EQ(plan.begin, 2u); EXPECT_EQ(plan.end, 4u);
    EXPECT_TRUE(plan.isolates_stereo);
}
TEST(StereoComposition, StereoBetweenAndAboveClientLayers) {
    const auto middle = client_range({stereo, mono, mono, stereo});
    EXPECT_EQ(middle.begin, 1u); EXPECT_EQ(middle.end, 3u);
    const auto top = client_range({mono, mono, stereo});
    EXPECT_EQ(top.begin, 0u); EXPECT_EQ(top.end, 2u);
}
TEST(StereoComposition, RequiredClientIntervalWinsOverLargerRun) {
    const auto plan = client_range({mono, mono, mono, stereo, required});
    EXPECT_EQ(plan.begin, 4u); EXPECT_EQ(plan.end, 5u);
    EXPECT_TRUE(plan.isolates_stereo);
}
TEST(StereoComposition, RequiredClientsCannotStraddleStereo) {
    const auto plan = client_range({required, stereo, required});
    EXPECT_FALSE(plan.isolates_stereo);
    EXPECT_EQ(plan.begin, 0u); EXPECT_EQ(plan.end, 3u);
    EXPECT_FALSE(client_range({{true, true, false}}).isolates_stereo);
}
TEST(StereoComposition, EmptyAndStereoOnlyFramesNeedNoClientTarget) {
    EXPECT_EQ(client_range({}).end, 0u);
    const auto plan = client_range({stereo, stereo, {false, false, true}});
    EXPECT_EQ(plan.end, 0u); EXPECT_TRUE(plan.isolates_stereo);
}
TEST(StereoComposition, MonoAndUnsetRejoinClientTarget) {
    auto layers = std::vector<CompositionLayer>{mono, stereo, mono};
    EXPECT_FALSE(client_range(layers).contains(1));
    layers[1].stereo = false;
    const auto plan = client_range(layers);
    EXPECT_EQ(plan.begin, 0u); EXPECT_EQ(plan.end, 3u);
}
}
