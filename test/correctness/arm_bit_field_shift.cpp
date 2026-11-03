#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

using namespace Halide;

// CodeGen_ARM rewrites ((x >> y) & m) << c, with a varying y, as
// (x << (c - y)) & (m << c). Check that it computes the same thing as the
// scalar path, which the rewrite doesn't touch, across types, mask forms,
// field widths and destinations.

namespace {

template<typename T>
bool test_type() {
    const Type t = type_of<T>();
    const int bits = t.bits();
    const int lanes = 128 / std::min(bits, 32);
    const int size = 1024;

    Buffer<T> in(size);
    std::mt19937 rng(bits);
    for (int i = 0; i < size; i++) {
        in(i) = (T)(((uint64_t)rng() << 32) | rng());
    }
    // Shift amounts the compiler can't bound, so the rewrite must not fire.
    Buffer<T> amounts(size);
    for (int i = 0; i < size; i++) {
        amounts(i) = (T)(rng() % bits);
    }

    Var x;
    for (int w : {1, 3}) {
        for (int c : {0, 1, bits / 2, bits - w, bits - 1}) {
            for (int form = 0; form < 4; form++) {
                for (int period : {8, bits}) {
                    Expr y = cast(t, x % period);
                    if (form == 2) {
                        y = amounts(x);
                    }
                    Expr field = in(x / 2) >> y;
                    if (form == 1 || form == 3) {
                        // A non-power-of-two divisor must not become a mask.
                        field = field % cast(t, (1 << w) + (form == 3));
                    } else {
                        field = field & cast(t, (1 << w) - 1);
                    }
                    Expr e = field << cast(t.with_code(halide_type_uint), c);

                    Func vec, scalar;
                    vec(x) = e;
                    scalar(x) = e;
                    vec.vectorize(x, lanes);

                    Buffer<T> v = vec.realize({size});
                    Buffer<T> s = scalar.realize({size});
                    for (int i = 0; i < size; i++) {
                        if (v(i) != s(i)) {
                            printf("Mismatch for %s, w = %d, c = %d, form = %d, period = %d at %d: "
                                   "vectorized %llx, scalar %llx\n",
                                   type_to_c_type(t, false).c_str(), w, c, form, period, i,
                                   (unsigned long long)v(i), (unsigned long long)s(i));
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

// Moving bit (x % 8) of a byte to bit 4 should be one ushl and an and, with no
// separate shl by the constant.
bool test_codegen() {
    Target target("arm-64-linux-no_asserts-no_bounds_query-no_runtime");
    ImageParam in(UInt(8), 1, "in");
    Var x;
    Func f("f");
    f(x) = ((in(x / 8) >> cast<uint8_t>(x % 8)) & 1) << 4;
    f.vectorize(x, 16);
    f.output_buffer().dim(0).set_min(0);
    in.dim(0).set_min(0);

    const std::string path = Internal::get_test_tmp_dir() + "arm_bit_field_shift.s";
    f.compile_to_assembly(path, {in}, "f", target);

    std::ifstream file(path);
    std::stringstream asm_text;
    asm_text << file.rdbuf();
    std::string line;
    bool has_ushl = false;
    while (std::getline(asm_text, line)) {
        std::istringstream words(line);
        std::string op;
        words >> op;
        if (op == "ushl") {
            has_ushl = true;
        } else if (op == "shl") {
            printf("Unexpected shl in:\n%s\n", asm_text.str().c_str());
            return false;
        }
    }
    if (!has_ushl) {
        printf("Expected a ushl in:\n%s\n", asm_text.str().c_str());
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    if (!(test_type<uint8_t>() &&
          test_type<uint16_t>() &&
          test_type<uint32_t>() &&
          test_type<uint64_t>() &&
          test_type<int8_t>() &&
          test_type<int16_t>() &&
          test_type<int32_t>() &&
          test_type<int64_t>())) {
        return 1;
    }

    if (!test_codegen()) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
