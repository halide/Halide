#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace Halide;

namespace {

// A 2x2 tile of 8-way int8 dot products (smmla) of a staged Func's bytes
// (e.g. weights decoded once, then reused) and another's. The staged Func's
// values are known to be in [0, 15]; those bounds are only of use to x86's
// codegen, and must not hide the tile's operands from Arm's.
std::string compile(bool bounded) {
    ImageParam w(UInt(8), 1, "w"), a(Int(8), 1, "a");
    Var x("x"), xo("xo"), xi("xi"), j("j");
    RDom k(0, 8);
    Func staged("staged"), f("f");
    staged(x) = bounded ? cast<int8_t>(w(x) & 15) : cast<int8_t>(w(x));
    f(x) = 0;
    f(x) += cast<int32_t>(staged(16 * (x / 2) + k)) * cast<int32_t>(a(8 * (x % 2) + k));
    staged.compute_root().vectorize(x, 16);
    f.bound(x, 0, 64).vectorize(x, 4);
    f.update()
        .split(x, xo, xi, 4)
        .split(xi, xi, j, 2)
        .reorder(k, j, xi, xo)
        .atomic()
        .vectorize(k)
        .vectorize(j)
        .vectorize(xi);

    std::string name = std::string("arm_dot_product_loaded_bounds_") + (bounded ? "bounded" : "unbounded");
    std::string file = Internal::get_test_tmp_dir() + name + ".s";
    Target target("arm-64-linux-arm_dot_prod-arm_i8mm-no_runtime-no_asserts-no_bounds_query");
    f.compile_to_assembly(file, {w, a}, name, target);

    std::ifstream in(file);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

}  // namespace

int main(int argc, char **argv) {
    for (bool bounded : {false, true}) {
        std::string s = compile(bounded);
        if (s.find("smmla") == std::string::npos) {
            printf("Expected smmla (%s). Assembly:\n%s\n", bounded ? "bounded" : "unbounded", s.c_str());
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
