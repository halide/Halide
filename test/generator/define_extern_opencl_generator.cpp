#include "Halide.h"

namespace {

class DefineExternOpenCLOutput : public Halide::Generator<DefineExternOpenCLOutput> {
public:
    Input<Buffer<int32_t, 1>> input{"input"};
    Output<Func> output{"output", Int(32), 1};

    Var x{"x"};
    // make_a_root is necessary as there doesn't seem to be a way to
    // get from Input<Buffer<int32_t>> to ExternFuncArgument otherwise.
    Func make_a_root{"make_a_root"};
    Func gpu_input{"gpu_input"};

    void generate() {
        make_a_root(x) = input(x);
        ExternFuncArgument arg = make_a_root;
        gpu_input.define_extern("gpu_input", {arg}, Halide::type_of<int32_t>(), 1, NameMangling::Default, Halide::DeviceAPI::OpenCL);

        output(x) = gpu_input(x) - 41;
    }

    void schedule() {
        make_a_root.compute_root();
        gpu_input.compute_root();
        if (get_target().has_feature(Target::OpenCL)) {
            Var block_x, thread_x;
            output.gpu_tile(x, block_x, thread_x, Expr(16),
                            TailStrategy::Auto, Halide::DeviceAPI::OpenCL);
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(DefineExternOpenCLOutput, define_extern_opencl)
