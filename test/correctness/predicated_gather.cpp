#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace Halide;

// Tail lanes of a vectorized loop with TailStrategy::GuardWithIf load through
// predicated gathers. Check they compute the right values, and that they are
// emitted as masked gather intrinsics rather than scalarized into a branch per
// lane (which made LLVM very slow on pipelines with many of them).

template<typename T>
bool test(int lanes, int stride) {
    const int size = 1000;  // Deliberately not a multiple of any vector width.

    Buffer<uint16_t> idx(size * stride);
    Buffer<T> lut(4096);
    for (int i = 0; i < idx.width(); i++) {
        idx(i) = (uint16_t)((i * 37 + 11) % 4096);
    }
    for (int i = 0; i < lut.width(); i++) {
        lut(i) = (T)((i * 7) % 101);
    }

    ImageParam in(UInt(16), 1), table(type_of<T>(), 1);
    Var x;
    Func f;
    Expr i = cast<int>(in(x * stride));
    f(x) = table(clamp(i, 0, 4095)) + table(clamp(4095 - i, 0, 4095)) + table(clamp(i, 0, 0));
    f.vectorize(x, lanes, TailStrategy::GuardWithIf);

    in.set(idx);
    table.set(lut);
    Buffer<T> out = f.realize({size});
    for (int x = 0; x < size; x++) {
        int j = idx(x * stride);
        T correct = (T)(lut(j) + lut(4095 - j) + lut(0));
        if (out(x) != correct) {
            printf("lanes=%d stride=%d: out(%d) = %f instead of %f\n",
                   lanes, stride, x, (double)out(x), (double)correct);
            return false;
        }
    }

    Target t = get_jit_target_from_environment();
    if (t.arch == Target::X86) {
        std::string ll = Internal::get_test_tmp_dir() + "predicated_gather.ll";
        f.compile_to_llvm_assembly(ll, {in, table}, "f", t);
        std::ifstream file(ll);
        std::stringstream contents;
        contents << file.rdbuf();
        if (contents.str().find("llvm.masked.gather") == std::string::npos) {
            printf("lanes=%d stride=%d: no masked gather in generated code\n", lanes, stride);
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    for (int lanes : {3, 4, 16, 64}) {
        for (int stride : {1, 3}) {
            if (!test<float>(lanes, stride) ||
                !test<uint8_t>(lanes, stride) ||
                !test<int16_t>(lanes, stride) ||
                !test<double>(lanes, stride)) {
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
