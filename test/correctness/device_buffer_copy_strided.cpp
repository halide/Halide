#include "Halide.h"
#include <algorithm>
#include <cstdio>
#include <random>

using namespace Halide;

namespace {

enum class Direction {
    HostToDevice,
    DeviceToHost,
    DeviceToDevice,
};

int value(int x, int y, int z) {
    return 1 + x + y * 100 + z * 10000;
}

// Copy a random region between two 3D buffers with random storage orders
// and check that exactly that region arrived.
bool test(const halide_device_interface_t *itf, std::mt19937 &rng, Direction dir) {
    std::vector<int> src_order = {0, 1, 2}, dst_order = {0, 1, 2};
    std::shuffle(src_order.begin(), src_order.end(), rng);
    std::shuffle(dst_order.begin(), dst_order.end(), rng);
    std::vector<int> src_size(3), dst_size(3);
    std::vector<std::pair<int, int>> region(3);
    for (int i = 0; i < 3; i++) {
        src_size[i] = 1 + rng() % 12;
        dst_size[i] = 1 + rng() % 12;
        int lo = rng() % std::min(src_size[i], dst_size[i]);
        int hi = rng() % std::min(src_size[i], dst_size[i]);
        if (lo > hi) {
            std::swap(lo, hi);
        }
        region[i] = {lo, hi - lo + 1};
    }

    Runtime::Buffer<int> src(src_size, src_order);
    Runtime::Buffer<int> dst(dst_size, dst_order);
    src.for_each_element([&](int x, int y, int z) { src(x, y, z) = value(x, y, z); });
    dst.fill(0);
    if (dir != Direction::HostToDevice) {
        src.set_host_dirty();
        src.copy_to_device(itf);
        // Clobber the host copy, so only data from the device can pass.
        src.fill(-1);
        src.set_host_dirty(false);
        src.set_device_dirty();
    }
    if (dir != Direction::DeviceToHost) {
        dst.set_host_dirty();
        dst.copy_to_device(itf);
    }

    Runtime::Buffer<int> src_crop = src.cropped(region);
    Runtime::Buffer<int> dst_crop = dst.cropped(region);
    const halide_device_interface_t *dst_itf = dir == Direction::DeviceToHost ? nullptr : itf;
    int err = itf->buffer_copy(nullptr, src_crop.raw_buffer(), dst_itf, dst_crop.raw_buffer());
    if (err) {
        printf("buffer_copy failed with error %d\n", err);
        return false;
    }
    if (dir != Direction::DeviceToHost) {
        dst.set_device_dirty();
        dst.copy_to_host();
    }

    bool ok = true;
    const Runtime::Buffer<int> &result = dst;
    result.for_each_element([&](int x, int y, int z) {
        bool inside = (x >= region[0].first && x < region[0].first + region[0].second &&
                       y >= region[1].first && y < region[1].first + region[1].second &&
                       z >= region[2].first && z < region[2].first + region[2].second);
        int correct = inside ? value(x, y, z) : 0;
        if (ok && result(x, y, z) != correct) {
            printf("Direction %d, src %dx%dx%d order %d%d%d, dst %dx%dx%d order %d%d%d, "
                   "region [%d+%d, %d+%d, %d+%d]: dst(%d, %d, %d) = %d instead of %d\n",
                   (int)dir,
                   src_size[0], src_size[1], src_size[2], src_order[0], src_order[1], src_order[2],
                   dst_size[0], dst_size[1], dst_size[2], dst_order[0], dst_order[1], dst_order[2],
                   region[0].first, region[0].second, region[1].first, region[1].second,
                   region[2].first, region[2].second,
                   x, y, z, result(x, y, z), correct);
            ok = false;
        }
    });
    return ok;
}

}  // namespace

int main() {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }
    if (target.has_feature(Target::Vulkan)) {
        printf("[SKIP] Vulkan does not yet copy correctly between buffers with different strides.\n");
        return 0;
    }
    const halide_device_interface_t *itf =
        get_device_interface_for_device_api(DeviceAPI::Default_GPU, target);

    std::mt19937 rng(0);
    for (int i = 0; i < 300; i++) {
        for (Direction dir : {Direction::HostToDevice,
                              Direction::DeviceToHost,
                              Direction::DeviceToDevice}) {
            if (!test(itf, rng, dir)) {
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
