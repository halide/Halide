#include "Halide.h"

#include <iostream>

using namespace Halide;

namespace Halide {
namespace Internal {

void generator_test() {
    GeneratorContext context(get_host_target().without_feature(Target::Profile));

    // Verify that the Generator's internal phase actually prevents unsupported
    // order of operations.
    {
        class Tester : public Generator<Tester> {
        public:
            GeneratorParam<int> gp0{"gp0", 0};
            GeneratorParam<float> gp1{"gp1", 1.f};
            GeneratorParam<uint64_t> gp2{"gp2", 2};

            Input<int> input{"input"};
            Output<Func> output{"output", Int(32), 1};

            void generate() {
                internal_assert(gp0 == 1);
                internal_assert(gp1 == 2.f);
                internal_assert(gp2 == (uint64_t)2);  // unchanged
                Var x;
                output(x) = input + gp0;
            }
            void schedule() {
                // empty
            }
        };

        Tester tester;
        tester.init_from_context(context);
        internal_assert(tester.phase == GeneratorBase::Created);

        // Verify that calling GeneratorParam::set() works.
        tester.gp0.set(1);

        tester.set_inputs_vector({{StubInput(42)}});
        internal_assert(tester.phase == GeneratorBase::InputsSet);

        // tester.set_inputs_vector({{StubInput(43)}});  // This will assert-fail.

        // Also ok to call in this phase.
        tester.gp1.set(2.f);

        tester.call_generate();
        internal_assert(tester.phase == GeneratorBase::GenerateCalled);

        // tester.set_inputs_vector({{StubInput(44)}});  // This will assert-fail.
        // tester.gp2.set(2);  // This will assert-fail.

        tester.call_schedule();
        internal_assert(tester.phase == GeneratorBase::ScheduleCalled);

        // tester.set_inputs_vector({{StubInput(45)}});  // This will assert-fail.
        // tester.gp2.set(2);  // This will assert-fail.
        // tester.sp2.set(202);  // This will assert-fail.
    }

    // Verify that set_inputs() works properly, even if the specific subtype of Generator is not known.
    {
        class Tester : public Generator<Tester> {
        public:
            Input<int> input_int{"input_int"};
            Input<float> input_float{"input_float"};
            Input<uint8_t> input_byte{"input_byte"};
            Input<uint64_t[4]> input_scalar_array{"input_scalar_array"};
            Input<Func> input_func_typed{"input_func_typed", Int(16), 1};
            Input<Func> input_func_untyped{"input_func_untyped", 1};
            Input<Func[]> input_func_array{"input_func_array", 1};
            Input<Buffer<uint8_t, 3>> input_buffer_typed{"input_buffer_typed"};
            Input<Buffer<>> input_buffer_untyped{"input_buffer_untyped"};
            Output<Func> output{"output", Float(32), 1};

            void generate() {
                Var x;
                output(x) = input_int +
                            input_float +
                            input_byte +
                            input_scalar_array[3] +
                            input_func_untyped(x) +
                            input_func_typed(x) +
                            input_func_array[0](x) +
                            input_buffer_typed(x, 0, 0) +
                            input_buffer_untyped(x, Halide::_);
            }
            void schedule() {
                // nothing
            }
        };

        Tester tester_instance;
        tester_instance.init_from_context(context);
        // Use a base-typed reference to verify the code below doesn't know about subtype
        GeneratorBase &tester = tester_instance;

        const int i = 1234;
        const float f = 2.25f;
        const uint8_t b = 0x42;
        const std::vector<uint64_t> a = {1, 2, 3, 4};
        Var x;
        Func fn_typed, fn_untyped;
        fn_typed(x) = make_const(Int(16), 38);
        fn_untyped(x) = 32.f;
        const std::vector<Func> fn_array = {fn_untyped, fn_untyped};

        Buffer<uint8_t> buf_typed(1, 1, 1);
        Buffer<float> buf_untyped(1);

        buf_typed.fill(33);
        buf_untyped.fill(34);

        // set_inputs() requires inputs in Input<>-decl-order,
        // and all inputs match type exactly.
        tester.set_inputs(i, f, b, a, fn_typed, fn_untyped, fn_array, buf_typed, buf_untyped);
        tester.call_generate();
        tester.call_schedule();

        Buffer<float> im = tester_instance.realize({1});
        internal_assert(im.dimensions() == 1);
        internal_assert(im.dim(0).extent() == 1);
        internal_assert(im(0) == 1475.25f) << "Expected 1475.25 but saw " << im(0);
    }

    // Verify that array inputs and outputs are typed correctly.
    {
        class Tester : public Generator<Tester> {
        public:
            Input<int[]> expr_array_input{"expr_array_input"};
            Input<Func[]> func_array_input{"input_func_array"};
            Input<Buffer<>[]> buffer_array_input{"buffer_array_input"};

            Input<int[]> expr_array_output{"expr_array_output"};
            Output<Func[]> func_array_output{"func_array_output"};
            Output<Buffer<>[]> buffer_array_output{"buffer_array_output"};

            void generate() {
            }
        };

        Tester tester_instance;

        static_assert(std::is_same_v<decltype(tester_instance.expr_array_input[0]), const Expr &>, "type mismatch");
        static_assert(std::is_same_v<decltype(tester_instance.expr_array_output[0]), const Expr &>, "type mismatch");

        static_assert(std::is_same_v<decltype(tester_instance.func_array_input[0]), const Func &>, "type mismatch");
        static_assert(std::is_same_v<decltype(tester_instance.func_array_output[0]), Func &>, "type mismatch");

        static_assert(std::is_same_v<decltype(tester_instance.buffer_array_input[0]), ImageParam>, "type mismatch");
        static_assert(std::is_same_v<decltype(tester_instance.buffer_array_output[0]), Func>, "type mismatch");
    }

    class GPTester : public Generator<GPTester> {
    public:
        GeneratorParam<int> gp{"gp", 0};
        Output<Func> output{"output", Int(32), 0};

        void generate() {
            internal_assert(get_target().has_feature(Target::Profile));
            output() = 0;
        }
        void schedule() {
        }

        // Test that we can override init_from_context() to modify the target
        // we use. (Generally speaking, your code probably should ever need to
        // do this; this code only does it for testing purposes. See comments
        // in Generator.h.)
        void init_from_context(const GeneratorContext &context) override {
            auto t = context.target().with_feature(Target::Profile);
            Generator<GPTester>::init_from_context(context.with_target(t));
        }
    };
    GPTester gp_tester;
    gp_tester.init_from_context(context);
    // Accessing the GeneratorParam will assert-fail if we
    // don't do some minimal setup here.
    gp_tester.set_inputs_vector({});
    gp_tester.call_generate();
    gp_tester.call_schedule();
    auto &gp = gp_tester.gp;

    // Verify that RDom parameter-pack variants can convert GeneratorParam to Expr
    RDom rdom(0, gp, 0, gp);

    // Verify that Func parameter-pack variants can convert GeneratorParam to Expr
    Var x, y;
    Func f, g;
    f(x, y) = x + y;
    g(x, y) = f(gp, gp);  // check Func::operator() overloads
    g(rdom.x, rdom.y) += f(rdom.x, rdom.y);
    g.update(0).reorder(rdom.y, rdom.x);  // check Func::reorder() overloads for RDom::operator RVar()

    // Verify that print() parameter-pack variants can convert GeneratorParam to Expr
    print(f(0, 0), g(1, 1), gp);
    print_when(true, f(0, 0), g(1, 1), gp);

    // Verify that Tuple parameter-pack variants can convert GeneratorParam to Expr
    Tuple t(gp, gp, gp);

    std::cout << "Generator test passed\n";
}

}  // namespace Internal
}  // namespace Halide

int main(int argc, char **argv) {
    Halide::Internal::generator_test();
    printf("Success!\n");
    return 0;
}
