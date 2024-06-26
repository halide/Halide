#include "Halide.h"
#include <stdio.h>

using namespace Halide;

template<typename T>
int check_result(Buffer<T> output, int n_types, int offset) {
    for (int x = 0; x < output.width(); x++) {
        T correct = n_types * (static_cast<uint16_t>(x) / 16) + offset;
        if (output(x) != correct) {
            printf("output(%d) = %d instead of %d\n",
                   (unsigned int)x, (unsigned int)output(x), (unsigned int)correct);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Target t(get_jit_target_from_environment());
    if (!t.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    Type types[] = {Int(8), Int(16), Int(32), Int(64),
                    UInt(8), UInt(16), UInt(32), UInt(64),
                    Float(32)};

    const int n_types = sizeof(types) / sizeof(types[0]);

    Func funcs[n_types];

    Var x("x"), xi("xi");

    Func out("out");

    Type result_type = UInt(64);
    if (!t.supports_type(result_type)) {
        result_type = UInt(32);
    }
    Expr e = cast(result_type, 0);
    int offset = 0;
    int skipped_types = 0;
    for (int i = 0; i < n_types; i++) {
        int off = 0;
        if ((types[i].is_int() || types[i].is_uint())) {
            // Not all targets support 64-bit integers.
            if (!t.supports_type(types[i])) {
                ++skipped_types;
                continue;
            }

            if (types[i].bits() <= 64) {
                off = (1 << (types[i].bits() - 4)) + 17;
            }
        }
        offset += off;

        funcs[i](x) = cast(types[i], x / 16 + off);
        e += cast(result_type, funcs[i](x));
        funcs[i].compute_at(out, x).gpu_threads(x);

        // Alternate between shared and global
        if (i & 1) {
            funcs[i].store_in(MemoryType::GPUShared);
        } else {
            funcs[i].store_in(MemoryType::Heap);
        }
    }

    out(x) = e;
    out.gpu_tile(x, xi, 23);

    Buffer<> output = out.realize({23 * 5});

    int result;
    if (result_type == UInt(32)) {
        result = check_result<uint32_t>(output, n_types - skipped_types, offset);
    } else {
        result = check_result<uint64_t>(output, n_types, offset);
    }
    if (result != 0) {
        printf("Failed!\n");
        return result;
    }

    printf("Success!\n");
    return 0;
}
