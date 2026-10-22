#include "Halide.h"

using namespace Halide;

// Exercises the full AOT scalar/buffer calling convention as seen by
// halide.runtime: a 2-D input buffer, input scalars of every supported width and
// signedness, and three outputs of different element types -- including a Tuple
// output, which lowers to several output buffers (a "structured" output).
class CallConv : public Generator<CallConv> {
public:
    // A compile-time enum GeneratorParam: it selects how `packed.0` is computed
    // and is baked into the generated code (it is NOT a runtime argument). It is
    // set at build time, e.g. `add_halide_library(... PARAMS combine=xor)`.
    enum class Combine { Add,
                         Sub,
                         Xor };
    GeneratorParam<Combine> combine{
        "combine",
        Combine::Add,
        {{"add", Combine::Add}, {"sub", Combine::Sub}, {"xor", Combine::Xor}}};

    Input<Buffer<uint8_t, 2>> input{"input"};

    Input<bool> s_bool{"s_bool"};
    Input<int8_t> s_i8{"s_i8"};
    Input<int16_t> s_i16{"s_i16"};
    Input<int32_t> s_i32{"s_i32"};
    Input<int64_t> s_i64{"s_i64"};
    Input<uint8_t> s_u8{"s_u8"};
    Input<uint16_t> s_u16{"s_u16"};
    Input<uint32_t> s_u32{"s_u32"};
    Input<uint64_t> s_u64{"s_u64"};
    Input<float> s_f32{"s_f32"};
    Input<double> s_f64{"s_f64"};

    // Distinct output element types.
    Output<Buffer<int64_t, 2>> total{"total"};
    Output<Buffer<double, 2>> scaled{"scaled"};
    // A Tuple output: lowers to two output buffers named "packed.0"/"packed.1".
    Output<Func> packed{"packed", {UInt(8), Int(32)}, 2};

    Var x, y;

    void generate() {
        Expr in = cast<int64_t>(input(x, y));
        Expr sum = in +
                   select(s_bool, cast<int64_t>(1), cast<int64_t>(0)) +
                   cast<int64_t>(s_i8) + cast<int64_t>(s_i16) +
                   cast<int64_t>(s_i32) + s_i64 +
                   cast<int64_t>(s_u8) + cast<int64_t>(s_u16) +
                   cast<int64_t>(s_u32) + cast<int64_t>(s_u64);

        // The enum GeneratorParam picks the operation at compile time.
        Expr combined;
        const Combine op = combine;
        switch (op) {
        case Combine::Add:
            combined = input(x, y) + s_u8;
            break;
        case Combine::Sub:
            combined = input(x, y) - s_u8;
            break;
        case Combine::Xor:
            combined = input(x, y) ^ s_u8;
            break;
        }

        total(x, y) = sum;
        scaled(x, y) = cast<double>(s_f32) * cast<double>(input(x, y)) + s_f64;
        packed(x, y) = Tuple(cast<uint8_t>(combined), cast<int32_t>(sum));
    }

    void schedule() {
    }
};

HALIDE_REGISTER_GENERATOR(CallConv, callconv)
