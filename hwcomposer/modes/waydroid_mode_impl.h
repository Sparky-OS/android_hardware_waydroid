/*
* Copyright © 2025 Waydroid Project.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice (including the
* next paragraph) shall be included in all copies or substantial
* portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
* BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
* ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
 */

#pragma once

#include "waydroid_mode.h"
#include "../stereo/composition.h"
#include "../stereo/surface.h"

template<class T>
class compositing_window_mode : public virtual waydroid_mode {
    skipped_layers_helper skipped_layers;

    T *derived() {
        return static_cast<T *>(this);
    }

  public:
    int setup_prepare(waydroid_hwc_composer_device_1 *, hwc_display_contents_1_t *contents) override {
        for (size_t i = 0; i < contents->numHwLayers; ++i) {
            skipped_layers.setup_for_each_layer(i, contents->hwLayers[i]);
        }
        return 0;
    }
    int prepare(hwc_layer_1 *hwc_layer, size_t hwc_layer_index) override {
        /* skipped layers have to be composited by SurfaceFlinger; so in order
         * to have correct z-ordering, we must ask SurfaceFlinger to composite
         * everything between the first and the last skipped layer. */
        if (skipped_layers.has_skipped_layers()) {
            if (skipped_layers.first_skipped() <= hwc_layer_index && hwc_layer_index <= skipped_layers.last_skipped()) {
                hwc_layer->compositionType = HWC_FRAMEBUFFER;
                return 0;
            }
        }

        if (hwc_layer->compositionType == HWC_FRAMEBUFFER) {
            hwc_layer->compositionType = HWC_OVERLAY;
        }
        // TODO: Handle HWC_SIDEBAND
        return 0;
    }

    int setup_set(waydroid_hwc_composer_device_1 *, hwc_display_contents_1_t *contents) override {
        skipped_layers.setup_set(contents->hwLayers, contents->numHwLayers);
        return 0;
    }
    int handle_layer(waydroid_hwc_composer_device_1 *pdev, hwc_layer_1 *hwc_layer, size_t i) override {
        if (i == skipped_layers.first_skipped()) {
            // Draw the content composited by SurfaceFlinger here
            if (hwc_layer->acquireFenceFd != -1) {
                close(hwc_layer->acquireFenceFd);
            }

            hwc_layer = skipped_layers.framebuffer_target_layer();
            assert(hwc_layer);
            i = skipped_layers.framebuffer_target_index();
        } else if (skipped_layers.first_skipped() < i && i <= skipped_layers.last_skipped()) {
            if (hwc_layer->acquireFenceFd != -1) {
                close(hwc_layer->acquireFenceFd);
            }
            return 0;
        } else if (hwc_layer == skipped_layers.framebuffer_target_layer()) {
            /*
             * There are two cases:
             * 1. We have skipped layers: In this case the framebuffer_target layer will be drawn at i = skipped_layers.first_skipped(),
             *    thus we can skip it here
             * 2. We have no skipped layers: In this case SurfaceFlinger has not done any compositing
             *    thus we can also skip the framebuffer_target layer.
             *    In this case we have to close acquireFenceFd though
             */
            if (!skipped_layers.has_skipped_layers() && hwc_layer->acquireFenceFd != -1) {
                close(hwc_layer->acquireFenceFd);
            }
            return 0;
        } else if (hwc_layer->compositionType != HWC_OVERLAY) {
            ALOGW("Encountered unsupported compositionType: %" PRId32, hwc_layer->compositionType);
            // TODO: Support HWC_BACKGROUND
            if (hwc_layer->acquireFenceFd != -1) {
                close(hwc_layer->acquireFenceFd);
            }
            return 0;
        } else if (!hwc_layer->handle) {
            return 0;
        }

        window *window = derived()->get_window(pdev);
        return apply_hwc_layer_to_window(pdev, hwc_layer, i, window);
    }

    int post_processing(waydroid_hwc_composer_device_1 *pdev, hwc_display_contents_1_t *contents) override {
        if (contents->flags & HWC_GEOMETRY_CHANGED) {
            neutralize_remaining_subsurfaces(pdev);
        }
        return 0;
    }
};
template<class T>
class non_compositing_window_mode : public virtual waydroid_mode {
    constexpr static size_t UNSET_VALUE = std::numeric_limits<size_t>::max();
    waydroid::stereo::ClientRange m_client_range;
    bool m_stereo_children = false;
    size_t m_draw_framebuffer_at {UNSET_VALUE};
    hwc_layer_1 *m_framebuffer_target {};
    size_t m_framebuffer_target_index {UNSET_VALUE};

    T *derived() { return static_cast<T *>(this); }

    window *get_window(waydroid_hwc_composer_device_1 *pdev) {
        auto *win = derived()->get_window(pdev);
        if (m_stereo_children) {
            // The old framebuffer-only child is desynchronized. Commit its
            // new client interval together with the isolated drawing children.
            for (auto& child : win->layers)
                if (child.subsurface) wl_subsurface_set_sync(child.subsurface);
        }
        return win;
    }

  public:
    int setup_prepare(waydroid_hwc_composer_device_1 *pdev, hwc_display_contents_1_t *contents) override {
        std::vector<waydroid::stereo::CompositionLayer> layers;
        for (size_t i = 0; i < contents->numHwLayers; ++i) {
            const auto& layer = contents->hwLayers[i];
            if (layer.compositionType == HWC_FRAMEBUFFER_TARGET) break;
            const auto layout = stereo_layout_for(pdev->display, i);
            const bool ignored = layer.flags & HWC_IS_CURSOR_LAYER;
            const bool stereo = !ignored && layout != 0 && layout != 1;
            // Keep effects the existing child path cannot reproduce in the
            // client target, including ordinary fading/coverage-blended UI.
            const bool client_only = (layer.flags & HWC_SKIP_LAYER) ||
                    layer.planeAlpha != 255 || layer.blending == HWC_BLENDING_COVERAGE;
            layers.push_back({stereo, client_only, ignored});
            m_stereo_children |= stereo;
        }
        if (pdev->display->subcompositor && pdev->display->viewporter)
            m_client_range = waydroid::stereo::client_range(layers);
        else
            m_client_range = {0, layers.size(), false};
        m_stereo_children &= m_client_range.isolates_stereo;
        return 0;
    }

    int prepare(hwc_layer_1 *layer, size_t i) override {
        layer->compositionType = m_client_range.contains(i) ? HWC_FRAMEBUFFER : HWC_OVERLAY;
        layer->hints &= ~HWC_HINT_CLEAR_FB;
        return 0;
    }

    int setup_set(waydroid_hwc_composer_device_1 *, hwc_display_contents_1_t *contents) override {
        for (size_t i = 0; i < contents->numHwLayers; ++i) {
            auto& layer = contents->hwLayers[i];
            if (m_draw_framebuffer_at == UNSET_VALUE && layer.compositionType == HWC_FRAMEBUFFER)
                m_draw_framebuffer_at = i;
            if (layer.compositionType == HWC_FRAMEBUFFER_TARGET) {
                m_framebuffer_target = std::addressof(layer);
                m_framebuffer_target_index = i;
            }
        }
        return 0;
    }

    int handle_layer(waydroid_hwc_composer_device_1 *pdev, hwc_layer_1 *layer, size_t i) override {
        int res = 0;
        if (i == m_draw_framebuffer_at && m_framebuffer_target && m_framebuffer_target->handle)
            res = apply_hwc_layer_to_window(pdev, m_framebuffer_target,
                    m_framebuffer_target_index, get_window(pdev));
        if (layer->compositionType == HWC_OVERLAY && layer->handle)
            return apply_hwc_layer_to_window(pdev, layer, i, get_window(pdev));
        if (layer->acquireFenceFd != -1) {
            close(layer->acquireFenceFd);
            layer->acquireFenceFd = -1;
        }
        return res;
    }

    int post_processing(waydroid_hwc_composer_device_1 *pdev, hwc_display_contents_1_t *) override {
        neutralize_remaining_subsurfaces(pdev);
        return 0;
    }
};
