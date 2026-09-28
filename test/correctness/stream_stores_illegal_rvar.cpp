#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("stream_stores_illegal_rvar", "Can't stream stores for hist.update(0) because it has a reduction variable that Halide cannot prove is safe to parallelize.", []() {
        Func hist("hist");
        Var x;
        ImageParam indices(Int(32), 1, "indices");
        RDom r(0, 100);

        hist(x) = 0;
        hist(indices(r)) += 1;

        // The scatter above means Halide can't prove r's iterations don't
        // collide, so stream_stores() is illegal on this update.
        hist.update(0).stream_stores();
    });
}
