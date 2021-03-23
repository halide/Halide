#include "simd_op_check.h"
#include "Halide.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace {

// This tests that we can correctly generate all the simd ops
using std::string;
using std::vector;

constexpr int max_i8 = 127;
constexpr int max_i16 = 32767;
constexpr int max_i32 = 0x7fffffff;
constexpr int max_u8 = 255;
constexpr int max_u16 = 65535;
const Expr max_u32 = UInt(32).max();

class SimdOpCheck : public SimdOpCheckTest {
public:
    SimdOpCheck(Target t, int w = 768, int h = 128)
        : SimdOpCheckTest(t, w, h) {}

        void add_tests() override {
            if (target.arch == Target::ARM) {
                check_neon_all();
            }
        }

        void check_neon_all() {
            Expr f64_1  = in_f64(x), f64_2 = in_f64(x + 16), f64_3 = in_f64(x + 32);
            Expr f32_1  = in_f32(x), f32_2 = in_f32(x + 16), f32_3 = in_f32(x + 32);
            Expr f16_1  = in_f16(x), f16_2 = in_f16(x + 16), f16_3 = in_f16(x + 32);
            Expr i8_1   = in_i8(x),  i8_2  = in_i8(x + 16),  i8_3  = in_i8(x + 32);
            Expr u8_1   = in_u8(x),  u8_2  = in_u8(x + 16),  u8_3  = in_u8(x + 32);
            Expr i16_1  = in_i16(x), i16_2 = in_i16(x + 16), i16_3 = in_i16(x + 32);
            Expr u16_1  = in_u16(x), u16_2 = in_u16(x + 16), u16_3 = in_u16(x + 32);
            Expr i32_1  = in_i32(x), i32_2 = in_i32(x + 16), i32_3 = in_i32(x + 32);
            Expr u32_1  = in_u32(x), u32_2 = in_u32(x + 16), u32_3 = in_u32(x + 32);
            Expr i64_1  = in_i64(x), i64_2 = in_i64(x + 16), i64_3 = in_i64(x + 32);
            Expr u64_1  = in_u64(x), u64_2 = in_u64(x + 16), u64_3 = in_u64(x + 32);
            Expr bool_1 = (f32_1 > 0.3f), bool_2 = (f32_1 < -0.3f), bool_3 = (f32_1 != -0.34f);

            // Table copied from the Cortex-A9 TRM.

            // In general neon ops have the 64-bit version, the 128-bit
            // version (ending in q), and the widening version that takes
            // 64-bit args and produces a 128-bit result (ending in l). We try
            // to peephole match any with vector, so we just try 64-bits, 128
            // bits, 192 bits, and 256 bits for everything.

            bool arm32 = (target.bits == 32);

            int vector_len = 8;
            check(arm32 ? "vadd.f32" : "fadd", vector_len / 2, f32_1 + f32_2); 
            check(arm32 ? "vadd.f16" : "fadd", vector_len, f16_1 + f16_2);

            /*
                // Eventually we'll test a variety of vector lengths 
                for (int w = 1; w <= 4; w++) {

                    // VADD     I, F    F, D    Add
                    check(arm32 ? "vadd.f32" : "fadd", 2 * w, f32_1 + f32_2); 
                    check(arm32 ? "vadd.f16" : "fadd", 2 * w, f16_1 + f16_2);
                    // check(arm32 ? "vadd.i8" : "add", 8 * w, i8_1 + i8_2);
                    // check(arm32 ? "vadd.i8" : "add", 8 * w, u8_1 + u8_2);
                    // check(arm32 ? "vadd.i16" : "add", 4 * w, i16_1 + i16_2);
                    // check(arm32 ? "vadd.i16" : "add", 4 * w, u16_1 + u16_2);
                    // check(arm32 ? "vadd.i32" : "add", 2 * w, i32_1 + i32_2);
                    // check(arm32 ? "vadd.i32" : "add", 2 * w, u32_1 + u32_2);
                    // check(arm32 ? "vadd.i64" : "add", 2 * w, i64_1 + i64_2);
                    // check(arm32 ? "vadd.i64" : "add", 2 * w, u64_1 + u64_2);
                }
            */
        }
private:
    const Var x{"x"}, y{"y"};
};
}  // namespace

int main(int argc, char **argv) {
    Target host = get_host_target();
    Target hl_target = Target("arm-64-android");
    printf("host is:      %s\n", host.to_string().c_str());
    printf("HL_TARGET is: %s\n", hl_target.to_string().c_str());

    // Create Test Object 
    SimdOpCheck test(hl_target);

    if (argc > 1) {
        test.filter = argv[1];
        test.set_num_threads(1);
    }

    if (getenv("HL_SIMD_OP_CHECK_FILTER")) {
        test.filter = getenv("HL_SIMD_OP_CHECK_FILTER");
    }

    // TODO: multithreading here is the cause of https://github.com/halide/Halide/issues/3669;
    // the fundamental issue is that we make one set of ImageParams to construct many
    // Exprs, then realize those Exprs on arbitrary threads; it is known that sharing
    // one Func across multiple threads is not guaranteed to be safe, and indeed, TSAN
    // reports data races, of which some are likely 'benign' (e.g. Function.freeze) but others
    // are highly suspect (e.g. Function.lock_loop_levels). Since multithreading here
    // was added just to avoid having this test be the last to finish, the expedient 'fix'
    // for now is to remove the multithreading. A proper fix could be made by restructuring this
    // test so that every Expr constructed for testing was guaranteed to share no Funcs
    // (Function.deep_copy() perhaps). Of course, it would also be desirable to allow Funcs, Exprs, etc
    // to be usable across multiple threads, but that is a major undertaking that is
    // definitely not worthwhile for present Halide usage patterns.
    test.set_num_threads(1);

    if (argc > 2) {
        // Don't forget: if you want to run the standard tests to a specific output
        // directory, you'll need to invoke with the first arg enclosed
        // in quotes (to avoid it being wildcard-expanded by the shell):
        //
        //    correctness_simd_op_check "*" /path/to/output
        //
        test.output_directory = argv[2];
    }

    bool success = test.test_all();

    if (!success) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}