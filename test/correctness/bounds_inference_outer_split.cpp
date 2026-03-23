#include "Halide.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;

// This was a failing case from https://github.com/halide/Halide/issues/1618

class CheckAllocationSize : public IRVisitor {

    using IRVisitor::visit;

    void visit(const Allocate *op) override {
        if (op->name == "input_cpy") {
            result = op->extents[0];
        } else {
            op->body.accept(this);
        }
    }

public:
    Expr result;
};

int main(int argc, char **argv) {
    Var x, y, xout, xin;

    ImageParam input(type_of<int16_t>(), 2);

    Func input_cpy("input_cpy");
    input_cpy(x, y) = input(x, y);

    Func input_cpy_2;
    input_cpy_2(x, y) = input_cpy(x, y);

    Func sum_stage;
    sum_stage(x, y) = (input_cpy_2(x, y - 4) +
                       input_cpy_2(x, y - 3) +
                       input_cpy_2(x, y - 2) +
                       input_cpy_2(x, y - 1) +
                       input_cpy_2(x, y));

    Func sum_stage_cpy;
    sum_stage_cpy(x, y) = sum_stage(x, y);

    Func sum_stage_cpy_2;
    sum_stage_cpy_2(x, y) = sum_stage_cpy(x, y);

    // bound the output to a fixed size
    sum_stage_cpy_2.bound(x, 0, 512);
    sum_stage_cpy_2.bound(y, 0, 512);

    // This stage was grossly overdimensioned by bounds inference: it
    // should only need 5 complete lines (512 * 5) = 2560 pixels.
    input_cpy.compute_at(sum_stage_cpy, y);

    input_cpy_2.compute_at(sum_stage_cpy, xout)
        .split(x, xout, xin, 32)
        .unroll(xout, 4);

    sum_stage_cpy
        .compute_at(sum_stage_cpy_2, y)
        .split(x, xout, xin, 32)
        .unroll(xout, 4);

    Module m = sum_stage_cpy_2.compile_to_module({input});

    CheckAllocationSize checker;
    m.functions()[0].body.accept(&checker);

    if (!is_const(checker.result, 512)) {
        std::cerr << m.functions()[0].body << "\n\n"
                  << "Allocation size was supposed to be 512 in dimension 0 in the stmt above\n";
        return 1;
    }

    printf("Success!\n");
    return 0;
}
