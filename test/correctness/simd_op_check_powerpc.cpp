#include "simd_op_check.h"

#include "Halide.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace {

class SimdOpCheckPowerPC : public SimdOpCheckTest {
public:
    SimdOpCheckPowerPC(Target t, int w = 768, int h = 128)
        : SimdOpCheckTest(t, w, h) {
        use_vsx = target.has_feature(Target::VSX);
        use_power_arch_2_07 = target.has_feature(Target::POWER_ARCH_2_07);
    }

    void add_tests() override {
        if (target.arch == Target::POWERPC) {
            check_altivec_all();
        }
    }

    void check_altivec_all() {
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x + 16), f32_3 = in_f32(x + 32);
        Expr f64_1 = in_f64(x), f64_2 = in_f64(x + 16), f64_3 = in_f64(x + 32);
        Expr i8_1 = in_i8(x), i8_2 = in_i8(x + 16), i8_3 = in_i8(x + 32);
        Expr u8_1 = in_u8(x), u8_2 = in_u8(x + 16), u8_3 = in_u8(x + 32);
        Expr i16_1 = in_i16(x), i16_2 = in_i16(x + 16), i16_3 = in_i16(x + 32);
        Expr u16_1 = in_u16(x), u16_2 = in_u16(x + 16), u16_3 = in_u16(x + 32);
        Expr i32_1 = in_i32(x), i32_2 = in_i32(x + 16), i32_3 = in_i32(x + 32);
        Expr u32_1 = in_u32(x), u32_2 = in_u32(x + 16), u32_3 = in_u32(x + 32);
        Expr i64_1 = in_i64(x), i64_2 = in_i64(x + 16), i64_3 = in_i64(x + 32);
        Expr u64_1 = in_u64(x), u64_2 = in_u64(x + 16), u64_3 = in_u64(x + 32);
        // Expr bool_1 = (f32_1 > 0.3f), bool_2 = (f32_1 < -0.3f), bool_3 = (f32_1 != -0.34f);

        // Basic AltiVec SIMD instructions.
        for (int w = 1; w <= 4; w++) {
            // Vector Integer Add Instructions.
            check("vaddsbs", 16 * w, i8_sat(i16(i8_1) + i16(i8_2)));
            check("vaddshs", 8 * w, i16_sat(i32(i16_1) + i32(i16_2)));
            check("vaddsws", 4 * w, i32_sat(i64(i32_1) + i64(i32_2)));
            check("vaddubm", 16 * w, i8_1 + i8_2);
            check("vadduhm", 8 * w, i16_1 + i16_2);
            check("vadduwm", 4 * w, i32_1 + i32_2);
            check("vaddubs", 16 * w, u8(min(u16(u8_1) + u16(u8_2), max_u8)));
            check("vadduhs", 8 * w, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
            check("vadduws", 4 * w, u32(min(u64(u32_1) + u64(u32_2), max_u32)));

            // Vector Integer Subtract Instructions.
            check("vsubsbs", 16 * w, i8_sat(i16(i8_1) - i16(i8_2)));
            check("vsubshs", 8 * w, i16_sat(i32(i16_1) - i32(i16_2)));
            check("vsubsws", 4 * w, i32_sat(i64(i32_1) - i64(i32_2)));
            check("vsububm", 16 * w, i8_1 - i8_2);
            check("vsubuhm", 8 * w, i16_1 - i16_2);
            check("vsubuwm", 4 * w, i32_1 - i32_2);
            check("vsububs", 16 * w, u8(max(i16(u8_1) - i16(u8_2), 0)));
            check("vsubuhs", 8 * w, u16(max(i32(u16_1) - i32(u16_2), 0)));
            check("vsubuws", 4 * w, u32(max(i64(u32_1) - i64(u32_2), 0)));
            check("vsububs", 16 * w, absd(i8_1, i8_2));
            check("vsubuhs", 16 * w, absd(i16_1, i16_2));
            check("vsubuws", 16 * w, absd(i32_1, i32_2));

            // Vector Integer Average Instructions.
            check("vavgsb", 16 * w, i8((i16(i8_1) + i16(i8_2) + 1) / 2));
            check("vavgub", 16 * w, u8((u16(u8_1) + u16(u8_2) + 1) / 2));
            check("vavgsh", 8 * w, i16((i32(i16_1) + i32(i16_2) + 1) / 2));
            check("vavguh", 8 * w, u16((u32(u16_1) + u32(u16_2) + 1) / 2));
            check("vavgsw", 4 * w, i32((i64(i32_1) + i64(i32_2) + 1) / 2));
            check("vavguw", 4 * w, u32((u64(u32_1) + u64(u32_2) + 1) / 2));

            // Vector Integer Maximum and Minimum Instructions
            check("vmaxsb", 16 * w, max(i8_1, i8_2));
            check("vmaxub", 16 * w, max(u8_1, u8_2));
            check("vmaxsh", 8 * w, max(i16_1, i16_2));
            check("vmaxuh", 8 * w, max(u16_1, u16_2));
            check("vmaxsw", 4 * w, max(i32_1, i32_2));
            check("vmaxuw", 4 * w, max(u32_1, u32_2));
            check("vminsb", 16 * w, min(i8_1, i8_2));
            check("vminub", 16 * w, min(u8_1, u8_2));
            check("vminsh", 8 * w, min(i16_1, i16_2));
            check("vminuh", 8 * w, min(u16_1, u16_2));
            check("vminsw", 4 * w, min(i32_1, i32_2));
            check("vminuw", 4 * w, min(u32_1, u32_2));

            // Vector Floating-Point Arithmetic Instructions
            check(use_vsx ? "xvaddsp" : "vaddfp", 4 * w, f32_1 + f32_2);
            check(use_vsx ? "xvsubsp" : "vsubfp", 4 * w, f32_1 - f32_2);
            check(use_vsx ? "xvmaddasp" : "vmaddfp", 4 * w, f32_1 * f32_2 + f32_3);
            // check("vnmsubfp", 4, f32_1 - f32_2 * f32_3);

            // Vector Floating-Point Maximum and Minimum Instructions
            check("vmaxfp", 4 * w, max(f32_1, f32_2));
            check("vminfp", 4 * w, min(f32_1, f32_2));
        }

        // Check these if target supports VSX.
        if (use_vsx) {
            for (int w = 1; w <= 4; w++) {
                // VSX Vector Floating-Point Arithmetic Instructions
                check("xvadddp", 2 * w, f64_1 + f64_2);
                check("xvmuldp", 2 * w, f64_1 * f64_2);
                check("xvsubdp", 2 * w, f64_1 - f64_2);
                check("xvaddsp", 4 * w, f32_1 + f32_2);
                check("xvmulsp", 4 * w, f32_1 * f32_2);
                check("xvsubsp", 4 * w, f32_1 - f32_2);
                check("xvmaxdp", 2 * w, max(f64_1, f64_2));
                check("xvmindp", 2 * w, min(f64_1, f64_2));
            }
        }

        // Check these if target supports POWER ISA 2.07 and above.
        // These also include new instructions in POWER ISA 2.06.
        if (use_power_arch_2_07) {
            for (int w = 1; w <= 4; w++) {
                check("vaddudm", 2 * w, i64_1 + i64_2);
                check("vsubudm", 2 * w, i64_1 - i64_2);

                check("vmaxsd", 2 * w, max(i64_1, i64_2));
                check("vmaxud", 2 * w, max(u64_1, u64_2));
                check("vminsd", 2 * w, min(i64_1, i64_2));
                check("vminud", 2 * w, min(u64_1, u64_2));
            }
        }
    }

private:
    bool use_power_arch_2_07{false};
    bool use_vsx{false};
    const Var x{"x"}, y{"y"};
};
}  // namespace

int main(int argc, char **argv) {
    return SimdOpCheckTest::main<SimdOpCheckPowerPC>(argc, argv);
}
