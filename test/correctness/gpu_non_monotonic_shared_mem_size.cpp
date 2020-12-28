#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    {
        Func f, g;
        Var x;
        const int size = 256;
        const int tile_size = 32;

        f(x) = x;
        g(x) = f(x * (size - 1 - x));

        Var xi;
        g.gpu_tile(x, xi, tile_size);
        f.compute_at(g, x);

        // The amount of f required for one tile of g is non-monotonic in
        // x. While we've done it here using a quadratic, this is
        // something that can come up for simpler producer-consumer
        // relationships too when the schedule is complex. If Halide just
        // applied naive interval arithmetic we'd try to allocate more
        // shared memory than exists and fail to run. Instead Halide runs
        // a loop on the CPU before launching the kernel to compute the
        // *actual* max shared mem required.

        // The most-quickly changing parts of x*(256-x) are at the start
        // and end. It's symmetric, so we'll use x coords [0, 31]. Bounds
        // will still be conservatively estimated *within* each thread
        // block, so the largest span of bytes allocated per thread block
        // will be:
        int max_elements = (tile_size - 1) * (size - 1);

        // Convert from max to extent
        max_elements += 1;

        // Convert from elements to bytes
        max_elements *= sizeof(int);

        // This is slightly larger than the theoretical max required
        // of (tile_size - 1) * (size - tile_size), but it's better
        // than what we get without iterating over all blocks on the
        // CPU to compute the max per block: (size - 1) * (size - 1) +
        // 1 elements, which causes a CUDA_ERROR_INVALID_VALUE at
        // kernel launch.

        printf("Case 1 should use %d bytes of shared memory\n", max_elements);

        g.realize(size);
    }

    {
        // Another example which uses an amount of shared memory
        // non-monotonic in multiple dimensions.
        Func f, g;
        Var x, y;
        f(x, y) = x + y;

        const int width = 64, height = 31;
        const int tile_width = 2, tile_height = 4;

        g(x, y) = f(x * (width - 1 - x), y * (height - 1 - y));

        Var xi, yi;
        g.gpu_tile(x, y, xi, yi, tile_width, tile_height);
        f.compute_at(g, x);

        int max_elements =
            (((tile_width - 1) * (width - 1)) *
             ((tile_height - 1) * (height - 1)));
        // Convert from max to extent
        max_elements += 1;

        // Convert from elements to bytes
        max_elements *= sizeof(int);
        printf("Case 2 should use %d bytes of shared memory\n", max_elements);

        g.realize(width, height);
    }

    {
        // The logic should also kick in for things compute_at blocks
        // stored in global memory. With it, we allocate 16MB of gpu
        // memory to back f. Without it, this will try to allocate
        // >68GB of GPU memory.
        Func f, g;
        Var x, y;
        f(x, y) = x + y;

        const int width = 64, height = 64;
        g(x, y) = f(x * (width - 1 - x), y * (height - 1 - y));

        Var xi, yi;
        const int tile_width = 2, tile_height = 2;
        g.gpu_tile(x, y, xi, yi, tile_width, tile_height);
        f.compute_at(g, x)
            .store_in(MemoryType::Heap);

        int max_elements =
            (((tile_width - 1) * (width - 1)) *
             ((tile_height - 1) * (height - 1)));
        // Convert from max to extent
        max_elements += 1;

        // Convert from elements to bytes
        max_elements *= sizeof(int);

        // Multiply by the number of thread blocks, because each block
        // gets its own slice of a global allocation.
        max_elements *= (width / tile_width) * (height / tile_height);
        printf("Case 3 should use %d bytes of global memory\n", max_elements);

        g.realize(width, height);
    }

    {
        // Finally, we have a case where there is both a precomputed
        // shared allocation and a precomputed global allocation.
        Func f1, f2, g;
        Var x, y;
        f1(x, y) = x + y;
        f2(x, y) = x + y;

        const int width = 32, height = 32;
        g(x, y) = f1(x * (width - 1 - x), y * (height - 1 - y)) +
                  f2(x * (width - 1 - x), y * (height - 1 - y));

        Var xi, yi;
        const int tile_width = 2, tile_height = 2;
        g.gpu_tile(x, y, xi, yi, tile_width, tile_height);
        f1.compute_at(g, x)
            .store_in(MemoryType::Heap);
        f2.compute_at(g, x)
            .store_in(MemoryType::GPUShared);

        int max_elements =
            (((tile_width - 1) * (width - 1)) *
             ((tile_height - 1) * (height - 1)));
        // Convert from max to extent
        max_elements += 1;

        // Convert from elements to bytes
        max_elements *= sizeof(int);

        // Multiply by the number of thread blocks, because each block
        // gets its own slice of a global allocation.
        int heap_bytes = max_elements * (width / tile_width) * (height / tile_height);
        printf("Case 4 should use %d bytes of global memory and %d bytes of shared memory\n",
               heap_bytes, max_elements);

        g.realize(width, height);
    }

    printf("Success!\n");
    return 0;
}
