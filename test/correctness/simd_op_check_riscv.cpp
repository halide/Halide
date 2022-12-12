#include "simd_op_check.h"

#include "Halide.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace {

class SimdOpCheckRISCV : public SimdOpCheckTest {
public:
    SimdOpCheckRISCV(Target t, int w = 768, int h = 128)
        : SimdOpCheckTest(t, w, h) {
    }

    void add_tests() override {
        if (target.arch == Target::RISCV &&
            target.has_feature(Target::RVV)) {
            check_rvv_all();
        }
    }

    void check_rvv_all() {
        Expr f64_1 = in_f64(x), f64_2 = in_f64(x + 16), f64_3 = in_f64(x + 32);
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x + 16), f32_3 = in_f32(x + 32);
        Expr i8_1 = in_i8(x), i8_2 = in_i8(x + 16), i8_3 = in_i8(x + 32);
        Expr u8_1 = in_u8(x), u8_2 = in_u8(x + 16), u8_3 = in_u8(x + 32);
        Expr i16_1 = in_i16(x), i16_2 = in_i16(x + 16), i16_3 = in_i16(x + 32);
        Expr u16_1 = in_u16(x), u16_2 = in_u16(x + 16), u16_3 = in_u16(x + 32);
        Expr i32_1 = in_i32(x), i32_2 = in_i32(x + 16), i32_3 = in_i32(x + 32);
        Expr u32_1 = in_u32(x), u32_2 = in_u32(x + 16), u32_3 = in_u32(x + 32);
        Expr i64_1 = in_i64(x), i64_2 = in_i64(x + 16), i64_3 = in_i64(x + 32);
        Expr u64_1 = in_u64(x), u64_2 = in_u64(x + 16), u64_3 = in_u64(x + 32);
        Expr bool_1 = (f32_1 > 0.3f), bool_2 = (f32_1 < -0.3f), bool_3 = (f32_1 != -0.34f);

        check("vmseq.vv", target.natural_vector_size<uint8_t>(), select(u8_1 == u8_2, u8(1), u8(2)));
    }

private:
    const Var x{"x"}, y{"y"};
};
}  // namespace

int main(int argc, char **argv) {
    if (Halide::Internal::get_llvm_version() < 160) {
        std::cout << "[SKIP] simd_op_check_riscv requires LLVM 16 or later.\n";
        return 0;
    }
    return SimdOpCheckTest::main<SimdOpCheckRISCV>(
        argc, argv,
        {
            Target("riscv-64-linux-rvv-vector_bits_128"),
            Target("riscv-64-linux-rvv-vector_bits_512"),
        });
}
