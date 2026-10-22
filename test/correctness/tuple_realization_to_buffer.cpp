#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("tuple_realization_to_buffer", "Cannot cast Realization with 3 elements to a Buffer", []() {
        Func f;
        Var x;

        f(x) = {x, x, x};

        Buffer<int> buf = f.realize({1024});
        (void)buf;
    });
}
