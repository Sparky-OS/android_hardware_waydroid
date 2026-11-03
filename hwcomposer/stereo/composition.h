// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <vector>

namespace waydroid::stereo {
struct CompositionLayer {
    bool stereo = false;
    bool client_only = false;
    bool ignored = false;
};
struct ClientRange {
    size_t begin = 0, end = 0;
    bool isolates_stereo = true;
    bool contains(size_t index) const { return begin <= index && index < end; }
};
// One client target has one position in the stack. Keep the largest ordinary
// interval on it; never fold layers across a stereo child or a required client.
ClientRange client_range(const std::vector<CompositionLayer>& layers);
}
