#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

extern "C" int extern_func() {
    return 0;
}

int main(int argc, char **argv) {
    // NEEDS REVIEW: best-effort expected substring; this path was inferred
    // from source analysis rather than a directly obvious user_error site.
    return error_test("extern_func_self_argument", "Stuck in a loop computing a realization order. Perhaps this pipeline has a loop involving f?", []() {
        Func f("f");

        f.define_extern("extern_func", {f}, Int(32), 2);
        f.infer_arguments();
    });
}
