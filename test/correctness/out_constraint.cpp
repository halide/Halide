#include "Halide.h"

#include <iostream>

// Verifies that constraints on the input ImageParam propagates to the output
// function.

using namespace Halide;
using namespace Halide::Internal;

void check_int(const Expr &expr, int expected) {
    if (!is_const(expr, expected)) {
        std::cerr << "Found expression " << expr << "; "
                  << "expected constant int " << expected << "\n";
        exit(1);
    }
}

constexpr int size = 10;

class CheckLoops : public IRVisitor {
public:
    int count = 0;

private:
    using IRVisitor::visit;

    void visit(const For *op) override {
        std::cout << "for(" << op->name << ", " << op->min << ", " << op->extent << ")\n";
        check_int(op->min, 0);
        check_int(op->extent, size);
        ++count;
        IRVisitor::visit(op);
    }
};

class Validator : public IRMutator {
    using IRMutator::mutate;

    Stmt mutate(const Stmt &s) override {
        CheckLoops c;
        s.accept(&c);

        if (c.count != 1) {
            std::cerr << "expected one loop, found " << c.count << "\n";
            exit(1);
        }

        return s;
    }
};

int main(int argc, char **argv) {
    ImageParam input(UInt(8), 1);
    input.dim(0).set_bounds(0, size);

    {
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
        if (!out.all_equal(42)) {
            std::cerr << "wrong output\n";
            exit(1);
        }
    }

    {
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
        if (!out.all_equal(42)) {
            std::cerr << "wrong output\n";
            exit(1);
        }
    }

    std::cout << "Success!\n";
    return 0;
}
