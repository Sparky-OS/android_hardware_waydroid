// SPDX-License-Identifier: Apache-2.0
#include "surface.h"
#include "../hwcomposer.h"
#include "kde-stereo-content-v1-client-protocol.h"
#include <log/log.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>
#include <sys/syscall.h>
#include <linux/memfd.h>
#include <limits.h>
#include <cstring>
#include <cinttypes>
#include <chrono>
#include <poll.h>
#include <cerrno>
#include <cmath>

using waydroid::stereo::Layout;
using waydroid::stereo::Size;

stereo_surface::~stereo_surface() { clear(); }
void stereo_surface::clear() {
    if (declaration) {
        kde_stereo_content_v1_set_content(declaration, KDE_STEREO_CONTENT_V1_CONTENT_NONE);
        kde_stereo_content_v1_destroy(declaration);
        declaration = nullptr;
    }
}
void surface_context::clear_stereo() { if (stereo) stereo->clear(); }

int32_t stereo_layout_for(display* d, size_t index) {
    std::lock_guard<std::mutex> lock(d->stereo_mutex);
    if (index >= d->stereo_layer_ids.size()) return 0;
    auto it = d->stereo_layouts.find(d->stereo_layer_ids[index]);
    return it == d->stereo_layouts.end() ? 0 : it->second;
}

static bool wait_for_buffer(display* d, stereo_surface& state) {
    const auto available = [&]() {
        for (const auto& slot : state.buffers) if (!slot->busy) return true;
        return state.buffers.size() < 3;
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (!available()) {
        if (wl_display_dispatch_queue_pending(d->display, d->stereo_queue) < 0) return false;
        if (available()) return true;
        // Read only the release queue here. The normal Wayland thread may be
        // waiting for windowsMutex, which the composition pass already holds.
        if (wl_display_prepare_read_queue(d->display, d->stereo_queue) < 0) continue;
        if (wl_display_flush(d->display) < 0 && errno != EAGAIN) {
            wl_display_cancel_read(d->display);
            return false;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            wl_display_cancel_read(d->display);
            return false;
        }
        pollfd fd{wl_display_get_fd(d->display), POLLIN, 0};
        const int ready = poll(&fd, 1, static_cast<int>(left));
        if (ready > 0 && (fd.revents & POLLIN)) {
            if (wl_display_read_events(d->display) < 0) return false;
        } else {
            wl_display_cancel_read(d->display);
            if (ready < 0 && errno == EINTR) continue;
            return false;
        }
    }
    return true;
}

static stereo_buffer* acquire_buffer(display* d, stereo_surface& state, Size size) {
    if (!wait_for_buffer(d, state)) return nullptr;
    for (auto& slot : state.buffers) {
        if (!slot->busy && slot->image->metadata.width == size.width &&
            slot->image->metadata.height == size.height) return slot.get();
    }
    // Bound storage even if the compositor stops releasing frames.
    auto slot = std::make_unique<stereo_buffer>();
    size_t position = 0;
    while (position < state.buffers.size() && state.buffers[position]->busy) ++position;
    if (position == state.buffers.size() && position == 3) return nullptr;
    if (!d->shm || size.width > INT_MAX / 4 ||
        size.height > size_t(INT_MAX) / (size_t(size.width) * 4)) return nullptr;
    const size_t length = size_t(size.width) * size.height * 4;
    int fd = syscall(SYS_memfd_create, "waydroid-stereo", MFD_CLOEXEC);
    if (fd < 0) return nullptr;
    if (ftruncate(fd, length)) { close(fd); return nullptr; }
    void* mapping = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) { close(fd); return nullptr; }
    auto* pool = wl_shm_create_pool(d->shm, fd, length);
    close(fd);
    if (!pool) { munmap(mapping, length); return nullptr; }
    auto* wlbuf = wl_shm_pool_create_buffer(pool, 0, size.width, size.height,
                                           size.width * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    if (!wlbuf) { munmap(mapping, length); return nullptr; }
    slot->image.reset(new buffer{});
    auto& image = *slot->image;
    image.wl_buffer = wlbuf;
    image.metadata = {size.height, size.width, size.width, HAL_PIXEL_FORMAT_BGRA_8888};
    image.isShm = true;
    image.shm_data = mapping;
    image.size = length;
    static const wl_buffer_listener listener = {
        [](void* data, wl_buffer*) { static_cast<stereo_buffer*>(data)->busy = false; }
    };
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wlbuf), d->stereo_queue);
    wl_buffer_add_listener(wlbuf, &listener, slot.get());
    if (position == state.buffers.size()) state.buffers.push_back(std::move(slot));
    else state.buffers[position] = std::move(slot);
    return state.buffers[position].get();
}

bool prepare_stereo(waydroid_hwc_composer_device_1* pdev, hwc_layer_1_t* layer,
                    size_t index, surface_context& surface, buffer*& buf, hwc_frect_t& crop) {
    auto* d = pdev->display;
    const auto layout = static_cast<Layout>(stereo_layout_for(d, index));
    if (layout == Layout::Unset || layout == Layout::Mono) {
        surface.clear_stereo();
        return false;
    }
    if (!surface.stereo) surface.stereo = std::make_unique<stereo_surface>();
    auto& state = *surface.stereo;
    const auto fail = [&](const char* reason) {
        state.clear();
        if (state.last_error != reason) {
            ALOGE("WaydroidStereo: layer index %zu layout %d shown in 2D: %s",
                  index, static_cast<int32_t>(layout), reason);
            state.last_error = reason;
        }
        return false;
    };
    if (!d->stereo_manager) {
        state.clear();
        if (!d->stereo_missing_reported) {
            ALOGE("WaydroidStereo: compositor has no kde_stereo_content_manager_v1; stereo requests shown in 2D");
            d->stereo_missing_reported = true;
        }
        return false;
    }
    if (!surface.viewport) return fail("viewporter is required for per-eye geometry");
    const auto& m = buf->metadata;
    const bool imported_shm = buf->isShm && d->gtype == GrallocType::GRALLOC_DEFAULT;
    if (imported_shm && !buf->shm_copy_valid)
        return fail("default gralloc SHM import failed; previous pixels are not stereo content");
    Size out{};
    if (const char* reason = waydroid::stereo::output_size(layout, {m.width, m.height}, &out))
        return fail(reason);
    // SurfaceFlinger crops the packed rectangle. Apply the same normalized
    // rectangle to each decoded eye, retaining the native sample lattice.
    const auto& source_crop = layer->sourceCropf;
    const double left = double(source_crop.left) * (out.width / 2) / m.width;
    const double top = double(source_crop.top) * out.height / m.height;
    const double right = double(source_crop.right) * (out.width / 2) / m.width;
    const double bottom = double(source_crop.bottom) * out.height / m.height;
    if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) ||
        !std::isfinite(bottom) || left < 0 || top < 0 || right > out.width / 2 ||
        bottom > out.height || right <= left || bottom <= top)
        return fail("per-eye crop outside native samples");
    const bool fractional = left != std::floor(left) || top != std::floor(top) ||
                            right != std::floor(right) || bottom != std::floor(bottom);
    const bool source_supported = kde_stereo_content_manager_v1_get_version(d->stereo_manager) >= 4;
    if (fractional && !source_supported)
        return fail("fractional per-eye edges need stereo protocol version 4; samples are not rescaled");
    waydroid::stereo::EyeSource eye_source{};
    if (!waydroid::stereo::transform_eye_source({left, top, right-left, bottom-top},
                                               {out.width / 2, out.height}, layer->transform, &eye_source))
        return fail("invalid per-eye transform geometry");
    // Keep the full native lattice for fractional crops. KWin selects the
    // fractional rectangle within each eye after the byte-preserving copy.
    const waydroid::stereo::EyeCrop eye_crop = fractional
        ? waydroid::stereo::EyeCrop{0, 0, out.width / 2, out.height}
        : waydroid::stereo::EyeCrop{uint32_t(left), uint32_t(top), uint32_t(right-left), uint32_t(bottom-top)};
    const bool full_crop = fractional ||
        (left == 0 && top == 0 && right == out.width / 2 && bottom == out.height);
    if (const char* reason = waydroid::stereo::cropped_size(layout, {m.width, m.height},
                                                         eye_crop, layer->transform, &out))
        return fail(reason);
    if (layer->transform || !full_crop ||
        (layout != Layout::SideBySideLeft && layout != Layout::SideBySideQuincunxLeft)) {
        if (m.format != HAL_PIXEL_FORMAT_RGBA_8888 && m.format != HAL_PIXEL_FORMAT_RGBX_8888 &&
            m.format != HAL_PIXEL_FORMAT_BGRA_8888)
            return fail("CPU repack needs readable RGBA, RGBX or BGRA8888; precision is never reduced");
        if (m.pixel_stride < m.width) return fail("invalid source stride");
        auto* slot = acquire_buffer(d, state, out);
        if (!slot) return fail("copy buffer release timed out after 250 ms or SHM allocation failed");
        void* source = nullptr;
        size_t source_size = 0, source_stride = 0;
        auto& mapper = android::GraphicBufferMapper::get();
        if (imported_shm) {
            // The existing importer has already produced tightly packed BGRA.
            // Gralloc2 does not supply CPU strides; do not lock or swizzle twice.
            source = buf->shm_data;
            source_size = buf->size;
            source_stride = size_t(m.width) * 4;
        } else {
            int32_t bytes_per_pixel = 0, bytes_per_stride = 0;
            if (mapper.lock(layer->handle, GRALLOC_USAGE_SW_READ_OFTEN,
                        android::Rect(m.width, m.height), &source,
                        &bytes_per_pixel, &bytes_per_stride))
                return fail("source buffer cannot be CPU-read (protected or unmappable)");
            // Use verified native handle metadata only for these known grallocs.
            if (bytes_per_pixel < 0 || bytes_per_stride < 0) {
                if (d->gtype != GrallocType::GRALLOC_GBM && d->gtype != GrallocType::GRALLOC_CROS) {
                    mapper.unlock(layer->handle);
                    return fail("gralloc cannot verify CPU pixel stride");
                }
                bytes_per_pixel = 4;
                bytes_per_stride = m.pixel_stride <= INT_MAX / 4 ? m.pixel_stride * 4 : 0;
            }
            if (bytes_per_pixel != 4 || bytes_per_stride < int64_t(m.width) * 4) {
                mapper.unlock(layer->handle);
                return fail("gralloc mapping has unexpected pixel or row stride");
            }
            source_stride = bytes_per_stride;
            source_size = source_stride * m.height;
        }
        auto* pixels = static_cast<uint8_t*>(slot->image->shm_data);
        bool ok = waydroid::stereo::repack_crop(layout, {m.width, m.height}, eye_crop, layer->transform,
                static_cast<uint8_t*>(source), source_size, source_stride,
                4, pixels, slot->image->size);
        if (!imported_shm) mapper.unlock(layer->handle);
        if (!ok) return fail("source bounds or packing rejected");
        // Wayland's mandatory ARGB8888 is BGRA byte order on Android x86.
        for (size_t i = 0; i < size_t(out.width) * out.height; ++i) {
            if (!imported_shm && m.format != HAL_PIXEL_FORMAT_BGRA_8888)
                std::swap(pixels[i*4], pixels[i*4+2]);
            if (m.format == HAL_PIXEL_FORMAT_RGBX_8888) pixels[i*4+3] = 255;
        }
        buf = slot->image.get();
        slot->busy = true;
        crop = {0, 0, float(out.width), float(out.height)};
    }
    if (!state.declaration) {
        state.declaration = kde_stereo_content_manager_v1_create(d->stereo_manager, surface.surface);
        if (!state.declaration) return fail("declaration allocation failed");
        uint64_t identity = 0;
        {
            std::lock_guard<std::mutex> lock(d->stereo_mutex);
            if (index < d->stereo_layer_ids.size()) identity = d->stereo_layer_ids[index];
        }
        ALOGI("WaydroidStereo: layer=%" PRIu64 " surface=%u layout=%d declared SBS left-first",
              identity, wl_proxy_get_id(reinterpret_cast<wl_proxy*>(surface.surface)),
              static_cast<int32_t>(layout));
    }
    kde_stereo_content_v1_set_content(state.declaration, KDE_STEREO_CONTENT_V1_CONTENT_SIDE_BY_SIDE_FULL);
    if (source_supported) {
        if (fractional) {
            kde_stereo_content_v1_set_eye_source(state.declaration,
                wl_fixed_from_double(eye_source.left), wl_fixed_from_double(eye_source.top),
                wl_fixed_from_double(eye_source.width), wl_fixed_from_double(eye_source.height));
            // A zero-copy SBS import still arrives with the packed crop.
            crop = {0, 0, float(out.width), float(out.height)};
        } else {
            kde_stereo_content_v1_set_eye_source(state.declaration,
                wl_fixed_from_int(-1), wl_fixed_from_int(-1), wl_fixed_from_int(-1), wl_fixed_from_int(-1));
        }
    }
    state.last_error.clear();
    return true;
}
