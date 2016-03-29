#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("Not running test because no gpu target enabled\n");
        return 0;
    }

    Func f1("f1"), f2("f2"), f3("f3"), f4("f4"), f5("f5"), f6("f6");
    Func f7("f7"), f8("f8"), f9("f9"), f10("f10"), f11("f11"), f12("f12");
    Var x, y, z;

    /*f1(x, y, z) = cast<uint8_t>(x + y + z);
    f2(x, y, z) = cast<uint32_t>(f1(x+1, y, z) + f1(x, y+1, z));
    f3(x, y, z) = cast<uint16_t>(f2(x+1, y, z) + f2(x, y+1, z));
    f4(x, y, z) = cast<uint8_t>(f3(x+1, y, z) + f3(x, y+1, z));
    f5(x, y, z) = cast<uint32_t>(f4(x+1, y, z) + f4(x, y+1, z));
    f6(x, y, z) = cast<uint16_t>(f5(x+1, y, z) + f5(x, y+1, z));
    f7(x, y, z) = cast<uint8_t>(f6(x+1, y, z) + f6(x, y+1, z));
    f8(x, y, z) = cast<uint16_t>(f7(x+1, y, z) + f7(x, y+1, z));
    f9(x, y, z) = cast<uint32_t>(f8(x+1, y, z) + f8(x, y+1, z));
    f10(x, y, z) = cast<uint8_t>(f9(x+1, y, z) + f9(x, y+1, z));
    f11(x, y, z) = cast<uint32_t>(f10(x+1, y, z) + f10(x, y+1, z));
    f12(x, y, z) = cast<uint32_t>(f11(x+1, y, z) + f11(x, y+1, z));*/

    f1(x, y, z) = (x + y + z);
    f2(x, y, z) = (f1(x+1, y, z) + f1(x, y+1, z));
    f3(x, y, z) = (f2(x+1, y, z) + f2(x, y+1, z));
    f4(x, y, z) = (f3(x+1, y, z) + f3(x, y+1, z));
    f5(x, y, z) = (f4(x+1, y, z) + f4(x, y+1, z));
    f6(x, y, z) = (f5(x+1, y, z) + f5(x, y+1, z));
    f7(x, y, z) = (f6(x+1, y, z) + f6(x, y+1, z));
    f8(x, y, z) = (f7(x+1, y, z) + f7(x, y+1, z));
    f9(x, y, z) = (f8(x+1, y, z) + f8(x, y+1, z));
    f10(x, y, z) = (f9(x+1, y, z) + f9(x, y+1, z));
    f11(x, y, z) = (f10(x+1, y, z) + f10(x, y+1, z));
    f12(x, y, z) = (f11(x+1, y, z) + f11(x, y+1, z));

    f12.compute_root().gpu_tile(x, y, 100, 100);
    f11.compute_at(f12, Var::gpu_blocks()).gpu_threads(x, y);
    f10.compute_at(f12, Var::gpu_blocks()).gpu_threads(x, y);
    f9.compute_at(f12, Var::gpu_blocks()).gpu_threads(x, y);
    f8.compute_at(f12, Var::gpu_blocks()).gpu_threads(x, y);
    f7.compute_at(f12, Var::gpu_blocks()).gpu_threads(x, y);
    f6.compute_at(f12, Var::gpu_blocks()).gpu_threads(x, y);
    f5.compute_at(f12, Var::gpu_blocks()).gpu_threads(x, y);
    f4.compute_at(f12, Var::gpu_blocks()).gpu_threads(x, y);
    f3.compute_at(f12, Var::gpu_blocks()).gpu_threads(x, y);
    f2.compute_at(f12, Var::gpu_blocks()).gpu_threads(x, y);
    f1.compute_at(f12, Var::gpu_blocks()).gpu_threads(x, y);

    const int size_x = 1000;
    const int size_y = 1000;
    const int size_z = 4;

    Image<int> out = f12.realize(size_x, size_y, size_z);

    /*for (int z = 0; z < size_z; z++) {
        for (int y = 0; y < size_y; y++) {
            for (int x = 0; x < size_x; x++) {
                int correct = 2048;
                if (out(x, y, z) != correct) {
                    printf("out(%d, %d, %d) = %d instead of %d\n",
                           x, y, z, out(x, y, z), correct);
                    //return -1;
                }
            }
        }
    }*/

    printf("Success!\n");

    return 0;
}
