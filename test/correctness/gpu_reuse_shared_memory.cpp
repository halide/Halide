#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("Not running test because no gpu target enabled\n");
        return 0;
    }

    Func f1("f1"), f2("f2"), f3("f3"), f4("f4");
    Var x, y, z;

    f1(x, y, z) = cast<uint8_t>(x + y + z);
    f2(x, y, z) = cast<uint32_t>(f1(x+1, y, z) + f1(x, y+1, z));
    f3(x, y, z) = cast<uint16_t>(f2(x+1, y, z) + f2(x, y+1, z));
    f4(x, y, z) = cast<uint8_t>(f3(x+1, y, z) + f3(x, y+1, z));

    f4.compute_root().gpu_tile(x, y, 8, 8);
    f3.compute_at(f4, Var::gpu_blocks()).gpu_threads(x, y);
    f2.compute_at(f4, Var::gpu_blocks()).gpu_threads(x, y);
    f1.compute_at(f4, Var::gpu_blocks()).gpu_threads(x, y);

    f4.realize(64, 64, 4);

    Image<int> out = f4.realize(64, 64, 4);
    for (int x = 0; x < 100; x++) {
        int correct = 3*x;
        if (out(x) != correct) {
            printf("out(%d) = %d instead of %d\n",
                   x, out(x), correct);
            return -1;
        }
    }

    printf("Success!\n");

    return 0;
}
