#include "Halide.h"

namespace {

class AllTypeNamesGeneric : public Halide::Generator<AllTypeNamesGeneric> {
public:
    Input<Func> input_i8{"input_i8", 1};
    Input<Func> input_i16{"input_i16", 1};
    Input<Func> input_i32{"input_i32", 1};
    Input<Func> input_i64{"input_i64", 1};
    Input<Func> input_u8{"input_u8", 1};
    Input<Func> input_u16{"input_u16", 1};
    Input<Func> input_u32{"input_u32", 1};
    Input<Func> input_u64{"input_u64", 1};
    Input<Func> input_f16{"input_f16", 1};
    Input<Func> input_f32{"input_f32", 1};
    Input<Func> input_f64{"input_f64", 1};
    Input<Func> input_bf16{"input_bf16", 1};
    Output<Func> output{"output", 1};

    void generate() {
        Var x;

        // Don't use float16 and bfloat16 arguments as they do not compile with C++ code generation.
        output(x) = cast<double>(input_i8(x) + input_i16(x) + input_i32(x) + input_i64(x)) +
                    cast<double>(input_u8(x) + input_u16(x) + input_u32(x) + input_u64(x)) +
                    input_f32(x) + input_f64(x);

        // set estimates for the autoschedulers
        input_i8.set_estimates({{0, 32}});
        input_i16.set_estimates({{0, 32}});
        input_i32.set_estimates({{0, 32}});
        input_i64.set_estimates({{0, 32}});
        input_u8.set_estimates({{0, 32}});
        input_u16.set_estimates({{0, 32}});
        input_u32.set_estimates({{0, 32}});
        input_u64.set_estimates({{0, 32}});
        input_f16.set_estimates({{0, 32}});
        input_f32.set_estimates({{0, 32}});
        input_f64.set_estimates({{0, 32}});
        input_bf16.set_estimates({{0, 32}});
        output.set_estimates({{0, 32}});

        if (!using_autoscheduler()) {
            output.vectorize(x, natural_vector_size<int64_t>()).compute_root();
        }
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(AllTypeNamesGeneric, all_type_names_generic)
HALIDE_REGISTER_GENERATOR_ALIAS(all_type_names, all_type_names_generic, {{"input_i8.type", "int8"}, {"input_i16.type", "int16"}, {"input_i32.type", "int32"}, {"input_i64.type", "int64"}, {"input_u8.type", "uint8"}, {"input_u16.type", "uint16"}, {"input_u32.type", "uint32"}, {"input_u64.type", "uint64"}, {"input_f16.type", "float16"}, {"input_f32.type", "float32"}, {"input_f64.type", "float64"}, {"input_bf16.type", "bfloat16"}, {"output.type", "float64"}})
