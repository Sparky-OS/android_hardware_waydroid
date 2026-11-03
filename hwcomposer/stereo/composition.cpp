// SPDX-License-Identifier: Apache-2.0
#include "composition.h"

namespace waydroid::stereo {
ClientRange client_range(const std::vector<CompositionLayer>& layers) {
    size_t first_required = layers.size(), last_required = 0;
    for (size_t i = 0; i < layers.size(); ++i) {
        if (!layers[i].ignored && layers[i].client_only) {
            if (first_required == layers.size()) first_required = i;
            last_required = i;
        }
    }
    ClientRange best;
    size_t best_count = 0;
    for (size_t begin = 0; begin < layers.size();) {
        if (layers[begin].stereo && !layers[begin].ignored) {
            ++begin;
            continue;
        }
        size_t end = begin, count = 0;
        while (end < layers.size() && (!layers[end].stereo || layers[end].ignored)) {
            if (!layers[end].ignored) ++count;
            ++end;
        }
        if (count && count >= best_count && (first_required == layers.size() ||
                (begin <= first_required && last_required < end))) {
            best = {begin, end, true};
            best_count = count;
        }
        begin = end;
    }
    if (first_required != layers.size() && !best.contains(first_required))
        return {0, layers.size(), false};
    return best;
}
}
