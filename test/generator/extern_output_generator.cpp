#include "Halide.h"

using namespace Halide;

namespace {

class ExternOutput : public Generator<ExternOutput> {
    Input<Buffer<int, 2>> input{"input"};
    Input<int> addend{"addend"};
    Output<Buffer<int, 2>> output{"output"};

    Func work;
    Var x, y;

public:
    void generate() {
        work(x, y) = input(x, y) * 2;

        std::vector<ExternFuncArgument> params = {work, addend};
        std::vector<Type> types = {Int(32)};
        std::vector<Var> args = {x, y};
        output.define_extern("extern_stage", params, types, args);
    }

    void schedule() {
        Var xo, yo;
        output.tile(x, y, xo, yo, x, y, 16, 16)
            .parallel(yo);

        work.compute_at(output, xo);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ExternOutput, extern_output)
