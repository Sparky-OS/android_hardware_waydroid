// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <hardware/hwcomposer.h>
#include <stddef.h>
#include <stdint.h>

// Only a device returning this exact version may be cast to this extension.
constexpr int WAYDROID_STEREO_IDENTITY_QUERY = 0x53544944;
constexpr int WAYDROID_STEREO_IDENTITY_V1 = 1;
struct waydroid_stereo_device_v1 : hwc_composer_device_1_t {
    // Called before prepare, for the primary display, in HWC1 array order.
    // The framebuffer target is excluded. Pointers are borrowed for this call.
    void (*set_layer_ids)(hwc_composer_device_1_t*, size_t, const uint64_t*);
};
