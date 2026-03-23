#include "Halide.h"

using namespace Halide;

int multi_thread_type_test(MemoryType memory_type) {
    Func f1("f1"), f2("f2"), f3("f3"), f4("f4"), f5("f5"), f6("f6");
    Var x, y, z;

    f1(x, y, z) = cast<uint8_t>(1);
    f2(x, y, z) = cast<uint32_t>(f1(x + 1, y, z) + f1(x, y + 1, z));
    f3(x, y, z) = cast<uint16_t>(f2(x + 1, y, z) + f2(x, y + 1, z));
    f4(x, y, z) = cast<uint16_t>(f3(x + 1, y, z) + f3(x, y + 1, z));
    f5(x, y, z) = cast<uint32_t>(f4(x + 1, y, z) + f4(x, y + 1, z));
    f6(x, y, z) = cast<uint8_t>(f5(x + 1, y, z) + f5(x, y + 1, z));

    Var thread_x, thread_y;
    f6.compute_root().gpu_tile(x, y, thread_x, thread_y, 1, 1);
    f5.compute_at(f6, x).gpu_threads(x, y).store_in(memory_type);
    f4.compute_at(f6, x).gpu_threads(x, y).store_in(memory_type);
    f3.compute_at(f6, x).gpu_threads(x, y).store_in(memory_type);
    f2.compute_at(f6, x).gpu_threads(x, y).store_in(memory_type);
    f1.compute_at(f6, x).gpu_threads(x, y).store_in(memory_type);

    const int size_x = 200;
    const int size_y = 200;
    const int size_z = 4;

    Buffer<uint8_t> out = f6.realize({size_x, size_y, size_z});

    uint8_t correct = 32;
    for (int z = 0; z < size_z; z++) {
        for (int y = 0; y < size_y; y++) {
            for (int x = 0; x < size_x; x++) {
                if (out(x, y, z) != correct) {
                    printf("out(%d, %d, %d) = %d instead of %d\n",
                           x, y, z, out(x, y, z), correct);
                    return 1;
                }
            }
        }
    }

    printf("OK\n");
    return 0;
}

int pyramid_test(MemoryType memory_type) {
    const int levels = 10;
    const int size_x = 100;
    const int size_y = 100;

    Var x, y, z, xo, xi, yo, yi, thread_x, thread_y;

    std::vector<Func> funcs(levels);

    funcs[0](x, y) = 1;
    for (int i = 1; i < levels; ++i) {
        funcs[i](x, y) = funcs[i - 1](2 * x, y);
    }

    funcs[levels - 1]
        .compute_root()
        .gpu_tile(x, y, thread_x, thread_y, 3, 4);
    for (int i = levels - 2; i >= 0; --i) {
        funcs[i]
            .compute_at(funcs[levels - 1], x)
            .split(x, xo, xi, 1 << (levels - i - 1))
            .gpu_threads(xo, y)
            .store_in(memory_type);
    }

    Buffer<int> out = funcs[levels - 1].realize({size_x, size_y});

    int correct = 1;
    for (int y = 0; y < size_y; y++) {
        for (int x = 0; x < size_x; x++) {
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                return 1;
            }
        }
    }

    printf("OK\n");
    return 0;
}

int inverted_pyramid_test(MemoryType memory_type) {
    const int levels = 6;
    const int size_x = 8 * 16 * 4;
    const int size_y = 8 * 16 * 4;

    Var x, y, z, yo, yi, yii, xo, xi, xii, thread_x, thread_y;

    std::vector<Func> funcs(levels);

    funcs[0](x, y) = 1;
    for (int i = 1; i < levels; ++i) {
        funcs[i](x, y) = funcs[i - 1](x / 2, y);
    }

    funcs[levels - 1]
        .compute_root()
        .tile(x, y, xi, yi, 64, 64)
        .gpu_blocks(x, y)
        .tile(xi, yi, xii, yii, 16, 16)
        .gpu_threads(xi, yi);
    for (int i = levels - 2; i >= 0; --i) {
        funcs[i]
            .compute_at(funcs[levels - 1], x)
            .tile(x, y, xi, yi, 4, 4)
            .gpu_threads(xi, yi)
            .store_in(memory_type);
    }

    funcs[levels - 1]
        .bound(x, 0, size_x)
        .bound(y, 0, size_y);

    Buffer<int> out = funcs[levels - 1].realize({size_x, size_y});

    int correct = 1;
    for (int y = 0; y < size_y; y++) {
        for (int x = 0; x < size_x; x++) {
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                return 1;
            }
        }
    }

    printf("OK\n");
    return 0;
}

int dynamic_shared_test(MemoryType memory_type) {
    Func f1, f2, f3, f4;
    Var x, xo, xi, thread_xo;

    f1(x) = x;
    f2(x) = f1(x) + f1(2 * x);
    f3(x) = f2(x) + f2(2 * x);
    f4(x) = f3(x) + f3(2 * x);

    f4.split(x, xo, xi, 16).gpu_tile(xo, thread_xo, 16);
    f3.compute_at(f4, xo).split(x, xo, xi, 16).gpu_threads(xi).store_in(memory_type);
    f2.compute_at(f4, xo).split(x, xo, xi, 16).gpu_threads(xi).store_in(memory_type);
    f1.compute_at(f4, xo).split(x, xo, xi, 16).gpu_threads(xi).store_in(memory_type);

    // The amount of shared memory required varies with x

    Buffer<int> out = f4.realize({500});
    for (int x = 0; x < out.width(); x++) {
        int correct = 27 * x;
        if (out(x) != correct) {
            printf("out(%d) = %d instead of %d\n",
                   x, out(x), correct);
            return 1;
        }
    }

    printf("OK\n");
    return 0;
}

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    for (auto memory_type : {MemoryType::GPUShared, MemoryType::Heap}) {
        printf("Running multi thread type test\n");
        if (multi_thread_type_test(memory_type) != 0) {
            return 1;
        }

        printf("Running pyramid test\n");
        if (pyramid_test(memory_type) != 0) {
            return 1;
        }

        printf("Running inverted pyramid test\n");
        if (inverted_pyramid_test(memory_type) != 0) {
            return 1;
        }

        printf("Running dynamic shared test\n");
        if (t.has_feature(Target::Vulkan) && ((t.os == Target::IOS) || t.os == Target::OSX)) {
            printf("Skipping test for Vulkan on iOS/OSX (MoltenVK doesn't support dynamic sizes for shared memory)!\n");
        } else {
            if (dynamic_shared_test(memory_type) != 0) {
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
