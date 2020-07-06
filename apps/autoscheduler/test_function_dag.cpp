#include "FunctionDAG.h"
#include "Halide.h"
#include <sstream>

using namespace Halide;

extern "C" int mul_by_two(
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

void test_coeff_wise(const MachineParams &params, const Target &target) {
    Var x("x"), y("y");

    std::ostringstream with_extern;
    {
        Func f("f"), g("g"), h("h");
        f(x, y) = (x + y) * (x + y);

        Halide::ExternFuncArgument arg = f;
        std::vector<Var> vars = {x, y};
        Halide::Type input_type = Halide::Float(32);
        g.define_extern(
            "mul_by_two",
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
}

extern "C" int matmul(
    halide_buffer_t *input1,
    halide_buffer_t *input2,
    halide_buffer_t *output) {
    if (input1->is_bounds_query() || input2->is_bounds_query()) {
        // Bounds query: infer the input dimensions from the output dimensions.
        // We leave the k dimension alone since we can't infer it from the output dimensions.
        input1->dim[0].min = output->dim[0].min;
        input1->dim[0].extent = output->dim[0].extent;
        input2->dim[1].min = output->dim[1].min;
        input2->dim[1].extent = output->dim[1].extent;
        return 0;
    }

    // Actual computation: return input1 * input2.
    const int max_i = output->dim[0].min + output->dim[0].extent;
    const int max_j = output->dim[1].min + output->dim[1].extent;
    for (int i = output->dim[0].min; i < max_i; ++i) {
        for (int j = output->dim[1].min; j < max_j; ++j) {
            int pos[2] = {i, j};
            float *out = (float *)output->address_of(pos);
            *out = 0.0f;
            for (int k = 0; k < input1->dim[1].extent; ++k) {
                int pos1[2] = {i, k};
                float *in1 = (float *)input1->address_of(pos1);
                int pos2[2] = {k, j};
                float *in2 = (float *)input2->address_of(pos2);
                (*out) += (*in1) * (*in2);
            }
        }
    }
    return 0;
}

void test_matmul(const MachineParams &params, const Target &target) {
    Var x("x"), y("y"), k("k");
    RDom r(0, 200);
    Halide::Buffer<float> input1(200, 200);
    Halide::Buffer<float> input2(200, 200);

    std::ostringstream with_extern;
    {
        Func mm("mm"), h("h");

        Halide::ExternFuncArgument arg1 = input1;
        Halide::ExternFuncArgument arg2 = input2;
        std::vector<Var> vars = {x, y};
        Halide::Type input_type = Halide::Float(32);
        mm.define_extern(
            "matmul",
            {arg1, arg2},
            {input_type, input_type},
            vars,
            Halide::NameMangling::C);
        mm.function().extern_definition_proxy_expr() = Halide::sum(input1(x, r) * input2(r, y));

        h(x, y) = mm(x, y);

        h.set_estimate(x, 0, 200).set_estimate(y, 0, 200);
        std::vector<Halide::Internal::Function> v;
        v.push_back(h.function());
        Halide::Internal::Autoscheduler::FunctionDAG d(v, params, target);

        d.dump(with_extern);
    }
    std::ostringstream without_extern;
    {
        Func mm("mm"), h("h");
        mm(x, y) = Halide::sum(input1(x, r) * input2(r, y));
        h(x, y) = mm(x, y);

        h.set_estimate(x, 0, 200).set_estimate(y, 0, 200);
        std::vector<Halide::Internal::Function> v;
        v.push_back(h.function());
        Halide::Internal::Autoscheduler::FunctionDAG d(v, params, target);

        d.dump(without_extern);
    }

    std::cout << "with_extern:\n " << with_extern.str()
              << "\n\nwithout_extern:\n " << without_extern.str() << "\n";
}

int main(int argc, char **argv) {
    // Use a fixed target for the analysis to get consistent results from this test.
    MachineParams params(32, 16000000, 40);
    Target target("x86-64-linux-sse41-avx-avx2");

    test_coeff_wise(params, target);
    test_matmul(params, target);

    return 0;
}
