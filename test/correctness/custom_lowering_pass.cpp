#include <Halide.h>
#include <stdio.h>

// This file demonstrates two example custom lowering passes. The
// first just makes sure the IR passes some test, and doesn't modify
// it. The second actually changes the IR in some useful way.

using namespace Halide;
using namespace Halide::Internal;

// Verify that all floating point divisions by constants have been
// converted to float multiplication.
class CheckForFloatDivision : public IRVisitor {
    void visit(const Div *op) {
        if (op->type.is_float() && is_const(op->b)) {
            std::cerr << "Found floating-point division by constant: " << Expr(op) << "\n";
            exit(-1);
        }
    }
};

bool pass_ran = false;
Stmt check_for_float_division(Stmt s) {
    CheckForFloatDivision checker;
    s.accept(&checker);
    pass_ran = true;
    return s;
}


// A mutator that injects code that counts floating point multiplies,
// and an extern function that it calls out to for the accounting.
#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

int multiply_count = 0;
extern "C" DLLEXPORT float record_float_mul(float arg) {
    multiply_count++;
    return arg;
}
HalideExtern_1(float, record_float_mul, float);

class CountMultiplies : public IRMutator {
    void visit(const Mul *op) {
        IRMutator::visit(op);
        if (op->type.is_float()) {
            expr = record_float_mul(expr);
        }
    }
};

Stmt count_multiplies(Stmt s) {
    return CountMultiplies().mutate(s);
}

int main(int argc, char **argv) {
    Func f;
    Var x;

    // OK
    f(x) = x / 2.4f + x / sin(x) + x * sin(x);
    f.add_custom_lowering_pass(&check_for_float_division);
    f.add_custom_lowering_pass(&count_multiplies);

    const int size = 10;
    f.realize(size);

    if (!pass_ran) {
        printf("Custom lowering pass didn't run!\n");
        return -1;
    }

    if (multiply_count != size * 2) {
        printf("The multiplies weren't all counted. Got %d instead of %d\n",
               multiply_count, size);
        return -1;
    }

    printf("Success!\n");
    return 0;
}
