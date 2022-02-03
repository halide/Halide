#include "Halide.h"

#include "g2_generator.h"

namespace Halide {
namespace Testing {

// TODO: buffers. imageparams? outputbuffers?
// TODO: pass in targetinfo.
Var x, y;

Func g2_func_impl(Func input, Expr offset, int scaling) {
    Func output;
    output(x, y) = input(x, y) * scaling + offset;
    output.compute_root();

    return output;
}

Func g2_func_impl_target(Target t, Func input, Expr offset, int scaling) {
    std::cout << "Hey Look, g_t is invoked with target=" << t << "\n";

    Func output;
    output(x, y) = input(x, y) * scaling + offset;
    output.compute_root();

    return output;
}

const auto g2_lambda_impl = [](Func input, Expr offset, int scaling,
                               Type ignored_type, bool ignored_bool, std::string ignored_string, int8_t ignored_int8) {
    std::cout << "Ignoring type: " << ignored_type << "\n";
    std::cout << "Ignoring bool: " << (int)ignored_bool << "\n";
    std::cout << "Ignoring string: " << ignored_string << "\n";
    std::cout << "Ignoring int8: " << (int)ignored_int8 << "\n";

    Func output = g2_func_impl(input, offset, scaling);
    // TODO output.vectorize(x, Target::natural_vector_size<int32_t>());

    return output;
};

Func g2_tuple_func_impl(Func input, Tuple offset, int scaling) {
    assert(input.values().size() == 2);
    assert(input.values()[0].type() == Int(32));
    assert(input.values()[1].type() == Float(64));

    assert(offset.size() == 2);
    assert(offset[0].type() == Int(32));
    assert(offset[1].type() == Float(64));

    Expr fscaling = Expr(0.5) * scaling;

    Func output;
    output(x, y) = Tuple(input(x, y)[0] * scaling + offset[0],
                         input(x, y)[1] * fscaling + offset[1]);
    output.compute_root();

    return output;
}

Pipeline g2_pipeline_impl(Func input, Expr offset, int scaling) {
    Expr fscaling = Expr(0.5) * scaling;

    Func output0, output1;
    output0(x, y) = input(x, y) * scaling + offset;
    output1(x, y) = input(x / 2, y / 2) * scaling + offset;

    output0.compute_root();
    output1.compute_root();

    return Pipeline({output0, output1});
}

}  // namespace Testing
}  // namespace Halide

HALIDE_REGISTER_G2(
    Halide::Testing::g2_func_impl,
    g2,
    Input("input", Int(32), 2),
    Input("offset", Int(32)),
    Constant("scaling", 2),
    Output("output", Int(32), 2))

HALIDE_REGISTER_G2(
    Halide::Testing::g2_func_impl_target,
    g2_t,
    Target(),
    Input("input", Int(32), 2),
    Input("offset", Int(32)),
    Constant("scaling", 2),
    Output("output", Int(32), 2))

HALIDE_REGISTER_G2(
    Halide::Testing::g2_lambda_impl,
    g2_lambda,
    Input("input", Int(32), 2),
    Input("offset", Int(32)),
    Constant("scaling", 2),
    Constant("ignored_type", Int(32)),
    Constant("ignored_bool", false),
    Constant("ignored_string", "qwerty"),
    Constant("ignored_int8", (int8_t)-27),
    Output("output", Int(32), 2))

HALIDE_REGISTER_G2(
    Halide::Testing::g2_tuple_func_impl,
    g2_tuple,
    Input("input", {Int(32), Float(64)}, 2),
    Input("offset", {Int(32), Float(64)}),
    Constant("scaling", 2),
    Output("output", {Int(32), Float(64)}, 2))

HALIDE_REGISTER_G2(
    Halide::Testing::g2_pipeline_impl,
    g2_pipeline,
    Input("input", Int(32), 2),
    Input("offset", Int(32)),
    Constant("scaling", 2),
    Output("output0", Int(32), 2),
    Output("output1", Int(32), 2))
