#include "Halide.h"
#include "expect_user_error.h"
using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_eager_inline_undefined", "eager_inline() was passed an undefined Func.", []() {
        Var x{"x"};
        Func undefined_producer{"undefined_producer"};  // never given a definition
        Func consumer{"consumer"};
        consumer(x) = x;

        // An undefined Func has no body to splice in, so eager_inline() rejects it.
        consumer.eager_inline({undefined_producer});
    });
}
