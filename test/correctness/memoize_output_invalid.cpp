#include <Halide.h>
using namespace Halide;

int main(int argc, char **argv) {
    return error_test("memoize_output_invalid", "Can't compile Pipeline with memoized output Func: f. Memoization is valid only on intermediate Funcs because it takes control of buffer allocation.", []() {
        Var x{"x"};
        Func f{"f"};
        f(x) = 0.0f;
        f(x) += 1;
        f.memoize();

        f.realize({3});
    });
}
