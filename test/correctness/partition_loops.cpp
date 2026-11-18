#include "Halide.h"
#include <stdio.h>
using namespace Halide;

int main(int argc, char *argv[]) {
    {
        Buffer<uint8_t> input(1024, 1024, 3);

        for (int c = 0; c < input.channels(); c++) {
            for (int y = 0; y < input.height(); y++) {
                for (int x = 0; x < input.width(); x++) {
                    input(x, y, c) = x + y + c;
                }
            }
        }

        Var x("x"), y("y"), c("c");

        Func clamped_input = Halide::BoundaryConditions::repeat_edge(input);

        // One of the possible conditions for partitioning loop 'f.s0.x' is
        // ((f.s0.x + g[0]) <= 1023) which depends on 'g'. Since 'g' is
        // only allocated inside f.s0.x, partition loops should not use this
        // condition to compute the epilogue/prologue.
        Func f("f"), g("g"), h("h");
        g(x, y, c) = x + y + c;
        g(x, y, 0) = x;
        h(x, y) = clamped_input(x + g(x, y, 0), y, 2);
        f(x, y, c) = select(h(x, y) < x + y, x + y, y + c);

        f.compute_root();

        Func output("output");
        output(x, y, c) = cast<float>(f(x, y, c));
        Buffer<float> im = output.realize({1024, 1024, 3});

        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < input.width(); x++) {
                for (int c = 0; c < input.channels(); c++) {
                    float correct = (input(std::min(2 * x, input.width() - 1), y, 2) < x + y) ? x + y : y + c;
                    if (im(x, y, c) != correct) {
                        printf("im(%d, %d, %d) = %f instead of %f\n",
                               x, y, c, im(x, y, c), correct);
                        return 1;
                    }
                }
            }
        }
    }

    // A loop partitioning bug from https://github.com/halide/Halide/issues/7742
    {
        Var x, y, x_outer, x_inner, y_x_inner_fused;
        Func f;
        f(x, y) = x + y;
        f.split(x, x_outer, x_inner, 2, TailStrategy::PredicateStores).fuse(y, x_inner, y_x_inner_fused);
        Pipeline p(f);
        p.realize({100, 100});
    }

    {
        // Two statements share a clamp node, but bind the clamped variable
        // differently, as happens with unrolled copies of a loop body. Each
        // use of the clamp must be simplified according to its own binding.
        using namespace Halide::Internal;
        Expr y = Variable::make(Int(32), "y");
        Expr t = Variable::make(Int(32), "t");
        Expr lo = Variable::make(Int(32), "lo");
        Expr hi = Variable::make(Int(32), "hi");
        Expr clamped = max(min(likely(t), hi), lo);
        Stmt body = Block::make(LetStmt::make("t", y, Store::make("f", clamped, 0)),
                                LetStmt::make("t", y * y, Store::make("f", clamped, 1)));
        Stmt s = For::make("y", 0, 100, ForType::Serial, Partition::Auto, DeviceAPI::None, body);
        s = partition_loops(s);

        bool unclamped = false;
        visit_with(s, [&](auto *, const Store *op) {
            if (is_const_one(op->index) && !op->value.as<Max>()) {
                unclamped = true;
            }
        });
        if (unclamped) {
            printf("Loop partitioning removed a clamp that was still needed:\n");
            std::cout << s << "\n";
            return 1;
        }
    }

    {
        // Where a likely predicate is false, a store does nothing and a load
        // returns zero.
        using namespace Halide::Internal;
        Expr x = Variable::make(Int(32), "x");
        Expr pred = Broadcast::make(likely(x < 50), 4);
        Expr idx = Ramp::make(x * 4, 1, 4);
        Expr load = Load::make(Int(32, 4), "g", idx, Buffer<>(), Parameter(), pred, ModulusRemainder(), false);
        Stmt body = Block::make(Store::make("f", Broadcast::make(1, 4), idx, Parameter(), pred, ModulusRemainder(), false),
                                Store::make("h", load, idx));
        Stmt s = For::make("x", 0, 100, ForType::Serial, Partition::Auto, DeviceAPI::None, body);
        s = partition_loops(s);

        int stores_to_f = 0, zero_stores_to_h = 0;
        visit_with(s, [&](auto *, const Store *op) {
            stores_to_f += op->name == "f";
            zero_stores_to_h += op->name == "h" && is_const_zero(op->value);
        });
        if (stores_to_f != 1 || zero_stores_to_h != 1) {
            printf("Loop partitioning did not simplify the epilogue:\n");
            std::cout << s << "\n";
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
