#include "Halide.h"
#include "halide_benchmark.h"
#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

// If a warp walks over an array in shared memory in a strided fashion, as might
// happen in the downsampling-in-x kernel below, warp lanes hit the same bank
// and you get bank conflicts. For illustration, assume we're dealing with a
// 32-bit type, so bank conflicts occur with stride 32. A strided access to
// shared with stride 16 would look like: [0 16 32 48 64 ...]. The even lanes
// all hit the same bank, and the odd lanes all hit the same bank. But if we
// insert a padding element between each 32 elements of the allocation, we can
// turn those indices into [0 16 33 49 66 ...] and now we have 32 distinct
// remainders modulo 32 -> no bank conflicts.

// An alternative approach is to transpose the allocation with a factor of
// 16. I.e. make an innermost dimension of size 16, and move it to be the
// outermost storage dimension. This moves the bottom four bits of the index to
// the high bits, shifting the higher indices over, making the indices into
// shared [0 1 2 3 4 ... ]

// The first approach requires the ability to split a storage dimension and then
// (mis)align or pad the inner axis, and the second approach requires the ability to
// split a storage dimension and then reorder it with the outer dimension.

// Below we try four approaches to this:
// 0) Do no shared memory staging, and just do strided loads from global
// 1) Stage in shared memory, but with no storage swizzling
// 2) The padding approach
// 3) The transpose approach

constexpr int stride = 16,
              filter_size = stride * 4,
              threads = 32,
              output_width = 64,
              input_width = output_width * stride + filter_size,
              height = 4096;

Func build(int mode, ImageParam in) {
    Var y("y"), x("x"), xo("xo"), xi("xi"), yi("yi"), so("so"), si("si");
    RDom k(0, filter_size);

    Func out("out");
    out(x, y) = sum(in(x * stride + k, y));

    out.gpu_tile(x, y, xi, yi, threads, 1, TailStrategy::RoundUp);

    // Give enough information to be able to vectorize the staging
    out.output_buffer().dim(0).set_min(0);
    in.dim(0).set_min(0);
    in.dim(1).set_stride(input_width);

    if (mode == 0) {
        // No further scheduling required
    } else if (mode == 1) {
        const int vec = 4;
        // Shared memory staging alone
        in.in(out)
            .compute_at(out, x)
            .store_in(MemoryType::GPUSharedAsync)
            .split(_0, xo, xi, threads * vec)
            .vectorize(xi, vec)
            .gpu_threads(xi)
            .reorder(xo, xi)
            .unroll(xo);
    } else if (mode == 2) {
        // Shared memory staging plus padding. Unfortunately we can't vectorize
        // the staging as anymore because we don't have the alignment we need.
        in.in(out)
            .compute_at(out, x)
            .store_in(MemoryType::GPUSharedAsync)
            .split(_0, xo, xi, threads, TailStrategy::RoundUp)
            .gpu_threads(xi)
            .reorder(xo, xi)
            .unroll(xo)
            .split_storage(_0, so, si, threads)
            .bound_storage(si, threads + 1);
    } else {
        assert(mode == 3);
        // Transpose
        in.in(out)
            .compute_at(out, x)
            .store_in(MemoryType::GPUSharedAsync)
            .split(_0, xo, xi, threads, TailStrategy::RoundUp)
            .gpu_threads(xi)
            .reorder(xo, xi)
            .unroll(xo)
            .split_storage(_0, so, si, stride)
            .reorder_storage(so, si);
    }
    return out;
}

int main() {
    Target target = get_jit_target_from_environment();
    if (!target.has_feature(Target::CUDA) || target.get_cuda_capability_lower_bound() < 80) {
        printf("[SKIP] Needs a CUDA target with compute capability 8.0+.\n");
        return 0;
    }
    Buffer<int32_t> in(input_width, height);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < input_width; x++) {
            in(x, y) = (x * 2 + y) & 1023;
        }
    }

    ImageParam ip(Int(32), 2, "in");
    ip.set(in);

    const char *nm[] = {"global-direct       ",
                        "shared no-pad       ",
                        "shared split+pad    ",
                        "shared split+transp "};
    double t[4];
    for (int m = 0; m < 4; m++) {
        Func f = build(m, ip);
        Buffer<int32_t> o(output_width, height);
        f.realize(o);
        o.copy_to_host();
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < output_width; x++) {
                int ref = 0;
                for (int k = 0; k < filter_size; k++) {
                    ref += in(x * stride + k, y);
                }
                if (o(x, y) != ref) {
                    printf("In mode %d, o(%d, %d) = %d instead of %d\n",
                           m, x, y, o(x, y), ref);
                    return 1;
                }
            }
        }
        t[m] = benchmark(3, 10, [&]() { f.realize(o); o.device_sync(); });
        printf("%s: %.3f ms\n", nm[m], t[m] * 1e3);
    }
    printf("pad    vs global: %.2fx  vs nopad: %.2fx\n", t[0] / t[2], t[1] / t[2]);
    printf("trans  vs global: %.2fx  vs nopad: %.2fx\n", t[0] / t[3], t[1] / t[3]);
    printf("Success!\n");
    return 0;
}
