#include "halide_benchmark.h"
#include <Halide.h>
#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

namespace {

// Size of the full image, and of the regions copied into and out of it.
constexpr int image_width = 4096, image_height = 4096;
constexpr int region_elems = 256 * 1024;

enum class Direction {
    HostToDevice,
    DeviceToHost,
    DeviceToDevice,
};

const char *name(Direction d) {
    switch (d) {
    case Direction::HostToDevice:
        return "host->device";
    case Direction::DeviceToHost:
        return "device->host";
    default:
        return "device->device";
    }
}

int value(int x, int y) {
    return x + y * image_width;
}

// Time copying a w x h region between a dense buffer of that shape and the
// top-left corner of the full image. When w == image_width the region is
// contiguous in the image, otherwise each of its rows is a separate chunk.
double test(const halide_device_interface_t *itf, Direction dir, int w, int h) {
    Runtime::Buffer<int> image(image_width, image_height);
    Runtime::Buffer<int> tile(w, h);
    image.fill(0);
    tile.fill(0);
    if (dir == Direction::HostToDevice || dir == Direction::DeviceToDevice) {
        tile.for_each_element([&](int x, int y) { tile(x, y) = value(x, y); });
    } else {
        image.for_each_element([&](int x, int y) { image(x, y) = value(x, y); });
    }
    image.set_host_dirty();
    image.copy_to_device(itf);
    if (dir == Direction::DeviceToDevice) {
        tile.set_host_dirty();
        tile.copy_to_device(itf);
    }
    if (dir == Direction::DeviceToHost) {
        // Clobber the host copy, so the check below sees only data
        // that came from the device.
        image.fill(-1);
        image.set_host_dirty(false);
    }
    Runtime::Buffer<int> region = image.cropped({{0, w}, {0, h}});

    halide_buffer_t *src, *dst;
    const halide_device_interface_t *dst_itf = itf;
    if (dir == Direction::DeviceToHost) {
        src = region.raw_buffer();
        dst = tile.raw_buffer();
        dst_itf = nullptr;
    } else {
        src = tile.raw_buffer();
        dst = region.raw_buffer();
    }

    auto copy = [&]() {
        if (dir == Direction::DeviceToHost) {
            // Otherwise halide_buffer_copy copies from the region's host memory.
            region.set_device_dirty();
        }
        int err = itf->buffer_copy(nullptr, src, dst_itf, dst);
        if (err) {
            printf("buffer_copy failed with error %d\n", err);
            exit(1);
        }
        err = itf->device_sync(nullptr, dst_itf ? dst : src);
        if (err) {
            printf("device_sync failed with error %d\n", err);
            exit(1);
        }
    };
    double time = benchmark(10, 10, copy);

    // Check the region arrived intact.
    if (dir != Direction::DeviceToHost) {
        image.set_device_dirty();
        image.copy_to_host();
    }
    const Runtime::Buffer<int> &result = dir == Direction::DeviceToHost ? tile : image;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (result(x, y) != value(x, y)) {
                printf("%s %dx%d: result(%d, %d) = %d instead of %d\n",
                       name(dir), w, h, x, y, result(x, y), value(x, y));
                exit(1);
            }
        }
    }

    return time;
}

}  // namespace

int main() {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] This test requires a GPU target.\n");
        return 0;
    }
    const halide_device_interface_t *itf =
        get_device_interface_for_device_api(DeviceAPI::Default_GPU, target);

    bool ok = true;
    for (Direction dir : {Direction::HostToDevice,
                          Direction::DeviceToHost,
                          Direction::DeviceToDevice}) {
        // The same number of bytes, as whole rows of the image or as a
        // region 1/64 as wide (and so made of 4096 separate rows).
        double contiguous = test(itf, dir, image_width, region_elems / image_width);
        double strided = test(itf, dir, image_width / 64, region_elems * 64 / image_width);
        printf("%-15s contiguous: %8.4f ms  strided: %8.4f ms  ratio: %5.2f\n",
               name(dir), contiguous * 1e3, strided * 1e3, strided / contiguous);
        // A strided region should be copied with a handful of
        // operations, not one per row.
        if (strided > 4 * contiguous) {
            printf("Copying a strided %s region is much slower than "
                   "copying a contiguous one of the same size.\n",
                   name(dir));
            ok = false;
        }
    }

    if (!ok) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
