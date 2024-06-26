#include "Halide.h"

// TODO: Add HalideExtern support for C++ mangling, hopefully using automatic argument type deduction
Halide::Expr extract_value_global(Halide::Expr arg) {
    return Halide::Internal::Call::make(Halide::type_of<int>(),
                                        "extract_value_global", {arg},
                                        Halide::Internal::Call::ExternCPlusPlus);
}

Halide::Expr extract_value_ns(Halide::Expr arg) {
    return Halide::Internal::Call::make(Halide::type_of<int>(),
                                        "HalideTest::extract_value_ns", {arg},
                                        Halide::Internal::Call::ExternCPlusPlus);
}

namespace my_namespace {
class my_class {
public:
    int foo;
};
namespace my_subnamespace {
struct my_struct {
    int foo;
};
}  // namespace my_subnamespace
}  // namespace my_namespace
union my_union {
    float a;
    int b;
};

HALIDE_DECLARE_EXTERN_CLASS_TYPE(my_namespace::my_class);
HALIDE_DECLARE_EXTERN_STRUCT_TYPE(my_namespace::my_subnamespace::my_struct);
HALIDE_DECLARE_EXTERN_UNION_TYPE(my_union);

class CPlusPlusNameManglingGenerator : public Halide::Generator<CPlusPlusNameManglingGenerator> {
public:
    // Use all the parameter types to make sure mangling works for each of them.
    // TODO: verify this provides full coverage.
    Input<Buffer<uint8_t, 1>> input{"input"};
    Input<int8_t> offset_i8{"offset_i8"};
    Input<uint8_t> offset_u8{"offset_u8"};
    Input<int16_t> offset_i16{"offset_i16"};
    Input<uint16_t> offset_u16{"offset_u16"};
    Input<int32_t> offset_i32{"offset_i32"};
    Input<uint32_t> offset_u32{"offset_u32"};
    Input<int64_t> offset_i64{"offset_i64"};
    Input<uint64_t> offset_u64{"offset_u64"};

    Input<bool> scale_direction{"scale_direction"};
    Input<float> scale_f{"scale_f"};
    Input<double> scale_d{"scale_d"};
    Input<int32_t *> ptr{"ptr"};
    Input<int32_t const *> const_ptr{"const_ptr"};
    Input<void *> void_ptr{"void_ptr"};
    Input<void const *> const_void_ptr{"const_void_ptr"};
    // 'string' is just a convenient struct-like thing that isn't special
    // cased by Halide; it will be generated as a void* (but const-ness
    // should be preserved).
    Input<std::string *> string_ptr{"string_ptr"};
    Input<std::string const *> const_string_ptr{"const_string_ptr"};

    // Test some manually-registered types. These won't be void *.
    Input<const my_namespace::my_class *> const_my_class_ptr{"const_my_class_ptr"};
    Input<const my_namespace::my_subnamespace::my_struct *> const_my_struct_ptr{"const_my_struct_ptr"};
    Input<const my_union *> const_my_union_ptr{"const_my_union_ptr"};

    Output<Buffer<double, 1>> output{"output"};

    void generate() {
        assert(get_target().has_feature(Target::CPlusPlusMangling));
        Var x("x");

        Expr offset = offset_i8 + offset_u8 + offset_i16 + offset_u16 +
                      offset_i32 + offset_u32 + offset_i64 + offset_u64 +
                      extract_value_global(ptr) + extract_value_ns(const_ptr);

        // No significance to the calculation here.
        output(x) = select(scale_direction, (input(x) * scale_f + offset) / scale_d,
                           (input(x) * scale_d + offset) / scale_f);
    }

    void schedule() {
        input.set_estimates({{0, 100}});
        offset_i8.set_estimate(0);
        offset_u8.set_estimate(0);
        offset_i16.set_estimate(0);
        offset_u16.set_estimate(0);
        offset_i32.set_estimate(0);
        offset_u32.set_estimate(0);
        offset_i64.set_estimate(0);
        offset_u64.set_estimate(0);
        scale_direction.set_estimate(1);
        scale_f.set_estimate(0);
        scale_d.set_estimate(0);
        ptr.set_estimate(nullptr);
        const_ptr.set_estimate(nullptr);
        void_ptr.set_estimate(nullptr);
        const_void_ptr.set_estimate(nullptr);
        string_ptr.set_estimate(nullptr);
        const_string_ptr.set_estimate(nullptr);
        const_my_class_ptr.set_estimate(nullptr);
        const_my_struct_ptr.set_estimate(nullptr);
        const_my_union_ptr.set_estimate(nullptr);
        output.set_estimates({{0, 100}});
    }
};

HALIDE_REGISTER_GENERATOR(CPlusPlusNameManglingGenerator, cxx_mangling)
