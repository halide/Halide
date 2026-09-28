#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("stream_loads_illegal_self", "Can't stream loads of \"f\" in f because a Stage cannot stream its own self-loads.", []() {
        Func f("f");
        Var x;

        f(x) = x;

        // A Stage can't stream loads of itself: naming f's own Stage in its own
        // stream_loads() request is illegal.
        f.stream_loads({f});
    });
}
