#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

IRPrinter irp(std::cerr);
int main(int argc, char **argv) {
    return error_test("bad_host_alignment", "is not aligned to a 512 bytes boundary.", []() {
        Func f;
        Var x, y;
        ImageParam in(UInt(8), 2);

        Buffer<uint8_t> param_buf(11, 10);
        param_buf.crop(0, 1, 10);

        in.set_host_alignment(512);
        f(x, y) = in(x, y);
        f.compute_root();

        in.set(param_buf);
        Buffer<uint8_t> result = f.realize({10, 10});
    });
}
