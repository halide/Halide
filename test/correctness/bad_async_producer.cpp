#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_async_producer", "The Func f is consumed by async Func g and has a compute_at location in between the store_at location and the compute_at location of g. This is only legal when f is both async and has a store_at location outside the store_at location of the consumer.", []() {
        Func f{"f"}, g{"g"}, h{"h"};
        Var x;

        f(x) = cast<uint8_t>(x + 7);
        g(x) = f(x);
        h(x) = g(x);

        // The schedule below is an error. It should really be:
        // f.store_root().compute_at(g, Var::outermost());
        // So that it's nested inside the consumer h.
        f.store_root().compute_at(h, x);
        g.store_root().compute_at(h, x).async();

        Buffer<uint8_t> buf = h.realize({32});
        (void)buf;
    });
}
