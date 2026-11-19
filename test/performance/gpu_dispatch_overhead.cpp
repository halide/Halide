#include "Halide.h"
#include "halide_benchmark.h"

#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

// Measures the fixed per-operation cost of a GPU runtime: launching a
// small kernel on device-resident buffers, uploading and downloading
// small buffers, allocating and freeing small device buffers, and the
// bandwidth of a large upload. The pipeline mimics a Gauss-Seidel
// relaxation step: three inputs and one output that get swapped
// between iterations, so that no input is ever dirty on the host.

namespace {

constexpr int W = 64, H = 64, C = 4;

double us(double seconds) {
    return seconds * 1e6;
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }
    const DeviceAPI api = get_default_device_api_for_target(target);
    printf("Target: %s\n", target.to_string().c_str());

    ImageParam field(Float(32), 3, "field"), knowns(Float(32), 3, "knowns");
    ImageParam conn(UInt(8), 3, "conn");
    Param<float> weight("weight");
    Param<uint8_t> threshold("threshold");

    Var x("x"), y("y"), c("c"), xi("xi"), yi("yi");
    Func f_clamped = BoundaryConditions::repeat_edge(field);
    Func relax("relax");
    Expr sum = f_clamped(x - 1, y, c) + f_clamped(x + 1, y, c) + f_clamped(x, y - 1, c) + f_clamped(x, y + 1, c);
    Expr connected = conn(x, y, 0) > threshold;
    relax(x, y, c) = select(connected, (sum + weight * knowns(x, y, c)) / (4.0f + weight), field(x, y, c));
    relax.gpu_tile(x, y, xi, yi, 16, 16).gpu_blocks(c);

    Callable step = relax.compile_to_callable({field, knowns, weight, conn, threshold}, target);

    Buffer<float> a(W, H, C), b(W, H, C), k(W, H, C);
    Buffer<uint8_t> cn(W, H, C);
    a.fill(1.0f);
    b.fill(0.0f);
    k.fill(2.0f);
    cn.fill(255);
    for (auto *buf : {&a, &b, &k}) {
        buf->set_host_dirty();
        buf->copy_to_device(api, target);
    }
    cn.set_host_dirty();
    cn.copy_to_device(api, target);

    // Warm up: compiles the kernel and creates the pipeline objects.
    Buffer<float> *in = &a, *out = &b;
    for (int i = 0; i < 10; i++) {
        step(*in, k, 30000.0f, cn, (uint8_t)128, *out);
        std::swap(in, out);
    }
    out->device_sync();

    const int launches = 100;

    // Back-to-back launches, synchronized once at the end.
    double t_dispatch = benchmark(10, 1, [&]() {
                            for (int i = 0; i < launches; i++) {
                                step(*in, k, 30000.0f, cn, (uint8_t)128, *out);
                                std::swap(in, out);
                            }
                            in->device_sync();
                        }) /
                        launches;

    // Launch and wait for each one.
    double t_dispatch_sync = benchmark(10, 1, [&]() {
                                 for (int i = 0; i < launches; i++) {
                                     step(*in, k, 30000.0f, cn, (uint8_t)128, *out);
                                     out->device_sync();
                                     std::swap(in, out);
                                 }
                             }) /
                             launches;

    // Device sync with nothing in flight.
    double t_sync = benchmark(10, 1, [&]() {
                        for (int i = 0; i < launches; i++) {
                            in->device_sync();
                        }
                    }) /
                    launches;

    // Small upload (the same size as the buffers above: 64 KiB).
    double t_upload = benchmark(10, 1, [&]() {
                          for (int i = 0; i < launches; i++) {
                              k.set_host_dirty();
                              k.copy_to_device(api, target);
                          }
                          k.device_sync();
                      }) /
                      launches;

    // Small upload followed by a launch that reads it.
    double t_upload_dispatch = benchmark(10, 1, [&]() {
                                   for (int i = 0; i < launches; i++) {
                                       k.set_host_dirty();
                                       step(*in, k, 30000.0f, cn, (uint8_t)128, *out);
                                       std::swap(in, out);
                                   }
                                   in->device_sync();
                               }) /
                               launches;

    // Small download of a buffer that was just written on the device.
    double t_download = benchmark(10, 1, [&]() {
                            for (int i = 0; i < launches; i++) {
                                k.set_device_dirty();
                                k.copy_to_host();
                            }
                        }) /
                        launches;

    // Allocate and free a small device buffer.
    Buffer<float> tmp(W, H, C);
    double t_malloc_free = benchmark(10, 1, [&]() {
                               for (int i = 0; i < launches; i++) {
                                   tmp.device_malloc(api, target);
                                   tmp.device_free();
                               }
                               tmp.device_sync();
                           }) /
                           launches;

    // Large upload: a 24 MP RGBA float16-sized buffer (~48 MB).
    const int big_w = 6000, big_h = 4000;
    Buffer<uint16_t> big(big_w, big_h);
    big.fill(7);
    big.set_host_dirty();
    big.copy_to_device(api, target);
    double t_big_upload = benchmark(5, 1, [&]() {
        big.set_host_dirty();
        big.copy_to_device(api, target);
        big.device_sync();
    });
    double t_big_download = benchmark(5, 1, [&]() {
        big.set_device_dirty();
        big.copy_to_host();
    });
    const double big_mb = big.size_in_bytes() / 1e6;

    // Verify the relaxation still computes the right thing after all of the above.
    a.set_device_dirty(false);
    b.set_device_dirty(false);
    b.fill(0.0f);
    b.set_host_dirty();
    a.fill(1.0f);
    a.set_host_dirty();
    k.fill(2.0f);
    k.set_host_dirty();
    step(a, k, 1.0f, cn, (uint8_t)128, b);
    b.copy_to_host();
    for (int yy = 0; yy < H; yy++) {
        for (int xx = 0; xx < W; xx++) {
            float correct = (4.0f + 2.0f) / 5.0f;
            if (std::abs(b(xx, yy, 0) - correct) > 1e-5f) {
                printf("b(%d, %d, 0) = %f instead of %f\n", xx, yy, b(xx, yy, 0), correct);
                return 1;
            }
        }
    }

    printf("launch, back to back:            %8.2f us\n", us(t_dispatch));
    printf("launch + device_sync:            %8.2f us\n", us(t_dispatch_sync));
    printf("device_sync, idle:               %8.2f us\n", us(t_sync));
    printf("upload 64 KiB:                   %8.2f us\n", us(t_upload));
    printf("upload 64 KiB + launch:          %8.2f us\n", us(t_upload_dispatch));
    printf("download 64 KiB:                 %8.2f us\n", us(t_download));
    printf("device_malloc + free 64 KiB:     %8.2f us\n", us(t_malloc_free));
    printf("upload %.0f MB:                    %8.2f ms (%.0f MB/s)\n", big_mb, t_big_upload * 1e3, big_mb / t_big_upload);
    printf("download %.0f MB:                  %8.2f ms (%.0f MB/s)\n", big_mb, t_big_download * 1e3, big_mb / t_big_download);

    printf("Success!\n");
    return 0;
}
