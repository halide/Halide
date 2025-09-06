#include "Halide.h"
#include <gtest/gtest.h>

#include <iostream>

// Verifies that constraints on the input ImageParam propagates to the output
// function.

using namespace Halide;
using namespace Halide::Internal;

namespace {

constexpr int size = 10;

struct CheckLoops : IRVisitor {
    int count = 0;

    using IRVisitor::visit;
    void visit(const For *op) override {
        // TODO: reuse matcher for EqConstantExpr
        EXPECT_TRUE(is_const(op->min, 0)) << "Found min " << op->min << "; expected 0";
        EXPECT_TRUE(is_const(op->extent, size)) << "Found expression " << op->min << "; expected " << size;
        ++count;
        IRVisitor::visit(op);
    }
};

class Validator : public IRMutator {
    using IRMutator::mutate;
    Stmt mutate(const Stmt &s) override {
        CheckLoops c;
        s.accept(&c);
        EXPECT_EQ(c.count, 1);
        return s;
    }
};

}  // namespace

TEST(OutConstraint, Constrained) {
    ImageParam input(UInt(8), 1);
    input.dim(0).set_bounds(0, size);

    Func f;
    Var x;
    f(x) = input(x);
    // Output must have the same size as the input.
    f.output_buffer().dim(0).set_bounds(input.dim(0).min(), input.dim(0).extent());
    f.add_custom_lowering_pass(new Validator);
    f.compile_jit();

    Buffer<uint8_t> dummy(size);
    dummy.fill(42);
    input.set(dummy);
    Buffer<uint8_t> out = f.realize({size});
    ASSERT_TRUE(out.all_equal(42)) << "wrong output";
}

TEST(OutConstraint, Unconstrained) {
    ImageParam input(UInt(8), 1);
    input.dim(0).set_bounds(0, size);

    Func f;
    Var x;
    f(x) = undef(UInt(8));
    RDom r(input);
    f(r.x) = cast<uint8_t>(42);

    f.add_custom_lowering_pass(new Validator);
    f.compile_jit();

    Buffer<uint8_t> dummy(size);
    input.set(dummy);
    Buffer<uint8_t> out = f.realize({size});
    ASSERT_TRUE(out.all_equal(42)) << "wrong output";
}
