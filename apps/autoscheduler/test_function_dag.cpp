#include "FunctionDAG.h"
#include "Halide.h"
#include <sstream>

using namespace Halide;

extern "C" int generate_output_vals(
    halide_buffer_t *input,
    halide_buffer_t *output) {
    if (input->is_bounds_query()) {
        // Bounds query: infer the input dimensions from the output dimensions. In
        // this example, the dimensions are exactly the same
        for (int i = 0; i < 2; ++i) {
            input->dim[i] = output->dim[i];
        }
        return 0;
    }

    // Actual computation: return 2 times x as an example. The first dimension is
    // the innermost, so iterate over it last to avoid inefficient memory access
    // patterns.
    for (int j = 0; j < input->dim[1].extent; ++j) {
        for (int i = 0; i < input->dim[0].extent; ++i) {
            float *out = (float *)output->host + i * output->dim[0].stride +
                         j * output->dim[1].stride;
            float *in = (float *)input->host + i * input->dim[0].stride +
                        j * input->dim[1].stride;
            (*out) = 2 * (*in);
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    // Use a fixed target for the analysis to get consistent results from this test.
    MachineParams params(32, 16000000, 40);
    Target target("x86-64-linux-sse41-avx-avx2");

    Var x("x"), y("y");

    std::ostringstream with_extern;
    {
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + y);

        Halide::ExternFuncArgument arg = f;
        std::vector<Var> vars = {x, y};
        Halide::Type input_type = Halide::Float(32);
        g.define_extern(
            "generate_output_vals",
            {arg},
            input_type,
            vars,
            Halide::NameMangling::C);
        g.function().extern_definition_proxy_expr() = f(x, y) * 2.0f;

        h(x, y) = g(x, y) * 2 + 1;

        h.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);
        std::vector<Halide::Internal::Function> v;
        v.push_back(h.function());
        Halide::Internal::Autoscheduler::FunctionDAG d(v, params, target);

        d.dump(with_extern);
    }

    std::ostringstream without_extern;
    {
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + y);
        g(x, y) = f(x, y) * 2.0f;
        h(x, y) = g(x, y) * 2 + 1;

        h.set_estimate(x, 0, 1000).set_estimate(y, 0, 1000);
        std::vector<Halide::Internal::Function> v;
        v.push_back(h.function());
        Halide::Internal::Autoscheduler::FunctionDAG d(v, params, target);

        d.dump(without_extern);
    }

    // Disabled for now: there is still work to do to populate the jacobian
    //assert(with_extern.str() == without_extern.str());

    return 0;
}
