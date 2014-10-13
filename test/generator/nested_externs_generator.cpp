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
    Param<float> value {1.0f, "value"};

    Func build() override {
        Func extern_stage_1("inner_extern_1"),
            extern_stage_2("inner_extern_2"),
            extern_stage_combine("inner_combine");

        extern_stage_1.define_extern("nested_externs_leaf",
                                     {value}, Float(32), 3);
        extern_stage_1.reorder_storage(extern_stage_1.args()[2],
                                        extern_stage_1.args()[0],
                                        extern_stage_1.args()[1]);
        extern_stage_1.compute_root();

        extern_stage_2.define_extern("nested_externs_leaf",
                                     {value+1}, Float(32), 3);
        extern_stage_2.reorder_storage(extern_stage_2.args()[2],
                                        extern_stage_2.args()[0],
                                        extern_stage_2.args()[1]);
        extern_stage_2.compute_root();

        extern_stage_combine.define_extern("nested_externs_combine",
                                           {extern_stage_1, extern_stage_2},
                                           Float(32), 3);
        extern_stage_combine.compute_root();

        set_interleaved(extern_stage_combine.output_buffer());

        return extern_stage_combine;
    }
};

// Basically a memset.
class NestedExternsLeaf : public Generator<NestedExternsLeaf> {
public:

    Param<float> value {1.0f, "value"};

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
    Param<float> value {1.0f, "value"};

    Func build() override {
        Func extern_stage_1("root_extern_1"),
            extern_stage_2("root_extern_2"),
            extern_stage_combine("root_combine");

        extern_stage_1.define_extern("nested_externs_inner",
                                     {value}, Float(32), 3);
        extern_stage_1.reorder_storage(extern_stage_1.args()[2],
                                        extern_stage_1.args()[0],
                                        extern_stage_1.args()[1]);
        extern_stage_2.define_extern("nested_externs_inner",
                                     {value+1}, Float(32), 3);
        extern_stage_2.reorder_storage(extern_stage_2.args()[2],
                                        extern_stage_2.args()[0],
                                        extern_stage_2.args()[1]);
        extern_stage_combine.define_extern("nested_externs_combine",
                                           {extern_stage_1, extern_stage_2},
                                           Float(32), 3);
        extern_stage_combine.reorder_storage(extern_stage_combine.args()[2],
                                             extern_stage_combine.args()[0],
                                             extern_stage_combine.args()[1]);
        set_interleaved(extern_stage_combine.output_buffer());

        Func wrapper;
        Var x, y, c;
        wrapper(x, y, c) = extern_stage_combine(x, y, c);
        wrapper.bound(c, 0, 3);

        //wrapper.reorder(c, x, y);
        extern_stage_1.compute_at(wrapper, y);
        extern_stage_2.compute_at(wrapper, y);
        extern_stage_combine.compute_at(wrapper, y);
        set_interleaved(wrapper.output_buffer());

        return wrapper;
    }
};

RegisterGenerator<NestedExternsCombine> gen1("nested_externs_combine");
RegisterGenerator<NestedExternsInner> gen2("nested_externs_inner");
RegisterGenerator<NestedExternsLeaf> gen3("nested_externs_leaf");
RegisterGenerator<NestedExternsRoot> gen4("nested_externs_root");

}  // namespace
