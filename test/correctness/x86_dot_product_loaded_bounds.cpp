#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace Halide;

namespace {

// A 4-way int8 dot product of a staged Func's bytes (e.g. weights decoded
// once, then reused) and another's. VNNI's vpdpbusd multiplies unsigned by
// signed bytes, so a signed-by-signed dot is computed in a bias form: the
// operand xor 0x80, less 128 times the other's sums. When the staged Func's
// values are known to be non-negative (here, in [0, 15]), its loads should
// be the unsigned operand directly, with no bias, even though the loads
// themselves are int8.
std::string compile(bool bounded) {
    ImageParam w(UInt(8), 1, "w"), a(Int(8), 1, "a");
    Var x("x");
    RDom r(0, 4);
    Func staged("staged"), f("f");
    staged(x) = bounded ? cast<int8_t>(w(x) & 15) : cast<int8_t>(w(x));
    f(x) = 0;
    f(x) += cast<int32_t>(staged(4 * x + r)) * cast<int32_t>(a(4 * x + r));
    staged.compute_root().vectorize(x, 64);
    f.vectorize(x, 16);
    f.update().atomic().vectorize(r).vectorize(x, 16);

    std::string name = std::string("x86_dot_product_loaded_bounds_") + (bounded ? "bounded" : "unbounded");
    std::string file = Internal::get_test_tmp_dir() + name + ".s";
    Target target("x86-64-linux-avx512_sapphirerapids-no_runtime-no_asserts-no_bounds_query");
    f.compile_to_assembly(file, {w, a}, name, target);

    std::ifstream in(file);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

bool has(const std::string &s, const std::string &op) {
    return s.find(op) != std::string::npos;
}

}  // namespace

int main(int argc, char **argv) {
    std::string bounded = compile(true), unbounded = compile(false);
    // (the control: without bounds, the bias form's correction)
    if (!has(unbounded, "vpdpbusd") || !has(unbounded, "vpsubd")) {
        printf("Expected the bias form (vpdpbusd, vpsubd) without bounds. Assembly:\n%s\n", unbounded.c_str());
        return 1;
    }
    if (!has(bounded, "vpdpbusd") || has(bounded, "vpsubd")) {
        printf("Expected vpdpbusd without the bias form's vpsubd. Assembly:\n%s\n", bounded.c_str());
        return 1;
    }
    printf("Success!\n");
    return 0;
}
