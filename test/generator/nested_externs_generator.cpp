#include "Halide.h"

using namespace Halide;

namespace {

template<typename T>
void set_interleaved(T &t) {
    t.dim(0).set_stride(3).dim(2).set_min(0).set_extent(3).set_stride(1);
}

// Add two inputs
class NestedExternsCombine : public Generator<NestedExternsCombine> {
public:
    Input<Buffer<float, 3>> input_a{"input_a"};
    Input<Buffer<float, 3>> input_b{"input_b"};
    Output<Buffer<>> combine{"combine"};  // unspecified type-and-dim will be inferred

    void generate() {
        Var x, y, c;
        combine(x, y, c) = input_a(x, y, c) + input_b(x, y, c);
    }

    void schedule() {
        set_interleaved(input_a);
        set_interleaved(input_b);
        set_interleaved(combine);
    }
};

// Call two extern stages then pass the two results to another extern stage.
class NestedExternsInner : public Generator<NestedExternsInner> {
public:
    Input<float> value{"value", 1.0f};
    Output<Buffer<float, 3>> inner{"inner"};

    void generate() {
        extern_stage_1.define_extern("nested_externs_leaf", {value}, Float(32), 3);
        extern_stage_2.define_extern("nested_externs_leaf", {value + 1}, Float(32), 3);
        extern_stage_combine.define_extern("nested_externs_combine",
                                           {extern_stage_1, extern_stage_2}, Float(32), 3);
        inner(x, y, c) = extern_stage_combine(x, y, c);
    }

    void schedule() {
        for (Func f : {extern_stage_1, extern_stage_2, extern_stage_combine}) {
            auto args = f.args();
            f.compute_root().reorder_storage(args[2], args[0], args[1]);
        }
        set_interleaved(inner);
    }

private:
    Var x, y, c;
    Func extern_stage_1, extern_stage_2, extern_stage_combine;
};

// Basically a memset.
class NestedExternsLeaf : public Generator<NestedExternsLeaf> {
public:
    Input<float> value{"value", 1.0f};
    Output<Buffer<float, 3>> leaf{"leaf"};

    void generate() {
        Var x, y, c;
        leaf(x, y, c) = value;
    }

    void schedule() {
        set_interleaved(leaf);
    }
};

// Call two extern stages then pass the two results to another extern stage.
class NestedExternsRoot : public Generator<NestedExternsRoot> {
public:
    Input<float> value{"value", 1.0f};
    Output<Buffer<float, 3>> root{"root"};

    void generate() {
        extern_stage_1.define_extern("nested_externs_inner", {value}, Float(32), 3);
        extern_stage_2.define_extern("nested_externs_inner", {value + 1}, Float(32), 3);
        extern_stage_combine.define_extern("nested_externs_combine",
                                           {extern_stage_1, extern_stage_2}, Float(32), 3);
        root(x, y, c) = extern_stage_combine(x, y, c);
    }

    void schedule() {
        for (Func f : {extern_stage_1, extern_stage_2, extern_stage_combine}) {
            auto args = f.args();
            f.compute_at(root, y).reorder_storage(args[2], args[0], args[1]);
        }
        set_interleaved(root);
        root.reorder_storage(c, x, y);
    }

private:
    Var x, y, c;
    Func extern_stage_1, extern_stage_2, extern_stage_combine;
};

}  // namespace

HALIDE_REGISTER_GENERATOR(NestedExternsCombine, nested_externs_combine)
HALIDE_REGISTER_GENERATOR(NestedExternsInner, nested_externs_inner)
HALIDE_REGISTER_GENERATOR(NestedExternsLeaf, nested_externs_leaf)
HALIDE_REGISTER_GENERATOR(NestedExternsRoot, nested_externs_root)
