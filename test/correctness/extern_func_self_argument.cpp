#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

extern "C" int extern_func() {
    return 0;
}

int main(int argc, char **argv) {
    return error_test("extern_func_self_argument", "Extern Func has itself as an argument", []() {
        Func f("f");

        f.define_extern("extern_func", {f}, Int(32), 2);
        f.infer_arguments();
    });
}
