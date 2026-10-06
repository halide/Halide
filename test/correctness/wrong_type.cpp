#include "Halide.h"
#include "expect_user_error.h"
using namespace Halide;

#ifdef NDEBUG
#error "wrong_type requires assertions"
#endif

int main(int argc, char **argv) {
    return error_test("wrong_type", "Type mismatch constructing Buffer. Can't construct Buffer<float", []() {
        Func f;
        Var x;
        f(x) = x;
        Buffer<float> im = f.realize({100});
        (void)im;
    });
}
