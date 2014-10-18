#include "Halide.h"

using namespace Halide;

namespace {

void set_interleaved(OutputImageParam m) {
    m.set_stride(0, 3);
    //m.set_stride(0, 3).set_stride(2, 1).set_bounds(2, 0, 3);
}

// Add two inputs
class NestedExternsCombine : public Generator<NestedExternsCombine> {
public:
    ImageParam input_a{ Float(32), 3, "a" };
    ImageParam input_b{ Float(32), 3, "b" };

    static constexpr const char* NAME = "nested_externs_combine";

    Func build() override {
        Func result("combine");
        Var x, y, c;

        set_interleaved(input_a);
        set_interleaved(input_b);

        result(x, y, c) = input_a(x, y, c) + input_b(x, y, c);

        set_interleaved(result.output_buffer());

        return result;
    }
};

// Call two extern stages then pass the two results to another extern stage.
class NestedExternsInner : public Generator<NestedExternsInner> {
public:
    Param<float> value {"value", 1.0f};

    static constexpr const char* NAME = "nested_externs_inner";

    Func build() override {
        // We can make an extern call to any (registered) Generator
        // by using call_extern_by_name() with the generator_name we're calling,
        // and a vector of ExternFuncArguments. We can (optionally) also
        // specify GeneratorParams for the Generator, and the function name we
        // expect that Generator to be linked under.
        Func extern_stage_1 = call_extern_by_name("nested_externs_leaf", {value});

        extern_stage_1.reorder_storage(extern_stage_1.args()[2],
                                        extern_stage_1.args()[0],
                                        extern_stage_1.args()[1]);
        extern_stage_1.compute_root();

        Func extern_stage_2 = call_extern_by_name("nested_externs_leaf", {value+1});
        extern_stage_2.reorder_storage(extern_stage_2.args()[2],
                                        extern_stage_2.args()[0],
                                        extern_stage_2.args()[1]);
        extern_stage_2.compute_root();

        // If the Generator is available at C++ compile time
        // (either via .h or inlined in the same file, as is the case here),
        // you can simply instantiate the Generator and make the call.
        // (Here we assume that it will be available at link time with
        // a function name that matches the generator name; we can
        // specify an optional function name otherwise.)
        Func extern_stage_combine = NestedExternsCombine().call_extern({extern_stage_1, extern_stage_2});
        extern_stage_combine.compute_root();

        set_interleaved(extern_stage_combine.output_buffer());

        return extern_stage_combine;
    }
};

// Basically a memset.
class NestedExternsLeaf : public Generator<NestedExternsLeaf> {
public:

    Param<float> value {"value", 1.0f};

    static constexpr const char* NAME = "nested_externs_leaf";

    Func build() override {
        Func f("leaf");
        Var x, y, c;
        f(x, y, c) = value;

        set_interleaved(f.output_buffer());

        return f;
    }
};

// Call two extern stages then pass the two results to another extern stage.
class NestedExternsRoot : public Generator<NestedExternsRoot> {
public:
    Param<float> value {"value", 1.0f};

    static constexpr const char* NAME = "nested_externs_root";

    Func build() override {
        Func extern_stage_1 = NestedExternsInner().call_extern({value});
        extern_stage_1.reorder_storage(extern_stage_1.args()[2],
                                        extern_stage_1.args()[0],
                                        extern_stage_1.args()[1]);

        Func extern_stage_2 = NestedExternsInner().call_extern({value+1});
        extern_stage_2.reorder_storage(extern_stage_2.args()[2],
                                        extern_stage_2.args()[0],
                                        extern_stage_2.args()[1]);

        Func extern_stage_combine = NestedExternsCombine().call_extern({extern_stage_1, extern_stage_2});
        extern_stage_combine.reorder_storage(extern_stage_combine.args()[2],
                                             extern_stage_combine.args()[0],
                                             extern_stage_combine.args()[1]);
        set_interleaved(extern_stage_combine.output_buffer());

        Func wrapper;
        Var x, y, c;
        wrapper(x, y, c) = extern_stage_combine(x, y, c);

        wrapper.reorder(c, x, y);
        extern_stage_1.compute_at(wrapper, y);
        extern_stage_2.compute_at(wrapper, y);
        extern_stage_combine.compute_at(wrapper, y);
        set_interleaved(wrapper.output_buffer());

        return wrapper;
    }
};

RegisterGenerator<NestedExternsCombine> register_combine_gen;
RegisterGenerator<NestedExternsInner> register_inner_gen;
RegisterGenerator<NestedExternsLeaf> register_leaf_gen;
RegisterGenerator<NestedExternsRoot> register_root_gen;

}  // namespace
