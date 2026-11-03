// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../wayland-hwc.h"
#include "layout.h"
struct kde_stereo_content_v1;
struct stereo_buffer {
    std::unique_ptr<buffer> image;
    bool busy = false;
};
struct stereo_surface {
    kde_stereo_content_v1* declaration = nullptr;
    std::vector<std::unique_ptr<stereo_buffer>> buffers;
    std::string last_error;
    ~stereo_surface();
    void clear();
};
// Called only for a per-layer subsurface, after waiting the acquire fence.
// On failure buf/crop remain original and the declaration is withdrawn.
bool prepare_stereo(waydroid_hwc_composer_device_1* device, hwc_layer_1_t* layer,
                    size_t index, surface_context& surface, buffer*& buf, hwc_frect_t& crop);
int32_t stereo_layout_for(display* d, size_t index);
