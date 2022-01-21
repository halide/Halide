#include "Halide.h"
#include "simd_op_check.h"

#include <stdio.h>
#include <string>

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace {

// This tests that we can correctly generate all the simd ops
using std::string;

class SimdOpCheck : public SimdOpCheckTest {
public:
    SimdOpCheck(Target t, int w = 768, int h = 128)
        : SimdOpCheckTest(t, w, h) {
    }

    bool can_run_code() const override {
        // If we can (target matches host), run the error checking Halide::Func.

        // TODO: Since features of Arm CPU cannot be obtained automatically from get_host_target(),
        // it is necessary to set "arm_fp16" feature explicitly to HL_JIT_TARGET.
        // Error is thrown if there is unacceptable mismatch between jit_target and host_target.
        Target jit_target = get_jit_target_from_environment();
        bool can_run_the_code =
            (target.arch == jit_target.arch &&
             target.bits == jit_target.bits &&
             target.os == jit_target.os);
        // A bunch of feature flags also need to match between the
        // compiled code and the host in order to run the code.
        for (Target::Feature f : {Target::ARMFp16, Target::NoNEON}) {
            if (target.has_feature(f) != jit_target.has_feature(f)) {
                can_run_the_code = false;
            }
        }
        return can_run_the_code;
    }

    void add_tests() override {
        check_neon_float16_all();
    }

    void check_neon_float16_all() {
        Expr f64_1 = in_f64(x), f64_2 = in_f64(x + 16), f64_3 = in_f64(x + 32);
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x + 16), f32_3 = in_f32(x + 32);
        Expr f16_1 = in_f16(x), f16_2 = in_f16(x + 16), f16_3 = in_f16(x + 32);
        Expr i8_1 = in_i8(x), i8_2 = in_i8(x + 16), i8_3 = in_i8(x + 32);
        Expr u8_1 = in_u8(x), u8_2 = in_u8(x + 16), u8_3 = in_u8(x + 32);
        Expr i16_1 = in_i16(x), i16_2 = in_i16(x + 16), i16_3 = in_i16(x + 32);
        Expr u16_1 = in_u16(x), u16_2 = in_u16(x + 16), u16_3 = in_u16(x + 32);
        Expr i32_1 = in_i32(x), i32_2 = in_i32(x + 16), i32_3 = in_i32(x + 32);
        Expr u32_1 = in_u32(x), u32_2 = in_u32(x + 16), u32_3 = in_u32(x + 32);
        Expr i64_1 = in_i64(x), i64_2 = in_i64(x + 16), i64_3 = in_i64(x + 32);
        Expr u64_1 = in_u64(x), u64_2 = in_u64(x + 16), u64_3 = in_u64(x + 32);
        Expr bool_1 = (f32_1 > 0.3f), bool_2 = (f32_1 < -0.3f), bool_3 = (f32_1 != -0.34f);

        // In general neon ops have the 64-bit version, the 128-bit
        // version (ending in q), and the widening version that takes
        // 64-bit args and produces a 128-bit result (ending in l). We try
        // to peephole match any with vector, so we just try 64-bits, 128
        // bits, 192 bits, and 256 bits for everything.
        struct TestParams {
            const int bits;
            ImageParam in_f;
            std::vector<std::pair<int, string>> vl_params;
            Expr f_1, f_2, f_3, u_1, i_1;
        };
        // clang-format off
        TestParams test_params[2] = {
            {32, in_f32, {{1, "s"}, {2, ".2s"}, {4, ".4s"}, { 8, ".4s"}}, f32_1, f32_2, f32_3, u32_1, i32_1},
            {16, in_f16, {{1, "h"}, {4, ".4h"}, {8, ".8h"}, {16, ".8h"}}, f16_1, f16_2, f16_3, u16_1, i16_1}
        };
        // clang-format on

        for (auto &test_param : test_params) {  // outer loop for {fp32, fp16}
            const int bits = test_param.bits;
            ImageParam in_f = test_param.in_f;
            Expr f_1 = test_param.f_1;
            Expr f_2 = test_param.f_2;
            Expr f_3 = test_param.f_3;
            Expr u_1 = test_param.u_1;
            Expr i_1 = test_param.i_1;

            for (auto &vl_param : test_param.vl_params) {
                const int vl = vl_param.first;
                const string suffix = vl_param.second;
                bool is_vector = vl > 1;

                check_neon_suffix("fabs", suffix, vl, abs(f_1));
                check_neon_suffix("fadd", suffix, vl, f_1 + f_2);
                check_neon_suffix(is_vector ? "fcmeq" : "fcm", suffix, vl, select(f_1 == f_2, 1.0f, 2.0f));
                check_neon_suffix(is_vector ? "fcmgt" : "fcm", suffix, vl, select(f_1 > f_2, 1.0f, 2.0f));
                check_neon_suffix("ucvtf", suffix, vl, cast(Float(bits), u_1));
                check_neon_suffix("scvtf", suffix, vl, cast(Float(bits), i_1));
                check_neon_suffix("fcvtzu", suffix, vl, cast(UInt(bits), f_1));
                check_neon_suffix("fcvtzs", suffix, vl, cast(Int(bits), f_1));
                check_neon_suffix("fdiv", suffix, vl, f_1 / f_2);
                check_neon_suffix("frinti", suffix, vl, round(f_1));
                check_neon_suffix("frintm", suffix, vl, floor(f_1));
                check_neon_suffix("frintp", suffix, vl, ceil(f_1));
                if (is_vector) {
                    check_neon_suffix("dup", suffix, vl, cast(Float(bits), y));
                }
                check_neon_suffix("ldr", "", vl, in_f(x));  // vector register is not used
                if (is_vector) {
                    check_neon_suffix("ld2", suffix, vl, in_f(x * 2) + in_f(x * 2 + 1));
                    check_neon_suffix("ld3", suffix, vl, in_f(x * 3) + in_f(x * 3 + 1) + in_f(x * 3 + 2));
                    check_neon_suffix("ld4", suffix, vl, in_f(x * 4) + in_f(x * 4 + 1) + in_f(x * 4 + 2) + in_f(x * 4 + 3));
                }
                check_neon_suffix("fmax", suffix, vl, max(f_1, f_2));
                check_neon_suffix("fmin", suffix, vl, min(f_1, f_2));
                check_neon_suffix(is_vector ? "fmla" : "fmadd", suffix, vl, f_1 + f_2 * f_3);
                check_neon_suffix(is_vector ? "fmls" : "fmsub", suffix, vl, f_1 - f_2 * f_3);
                check_neon_suffix("fmul", suffix, vl, f_1 * f_2);
                check_neon_suffix("fneg", suffix, vl, -f_1);
                check_neon_suffix("frecpe", suffix, vl, fast_inverse(f_1));
                check_neon_suffix("frecps", suffix, vl, fast_inverse(f_1));
                check_neon_suffix("frsqrte", suffix, vl, fast_inverse_sqrt(f_1));
                check_neon_suffix("frsqrts", suffix, vl, fast_inverse_sqrt(f_1));
                check_neon_suffix("fsqrt", suffix, vl, sqrt(f_1));
                check_neon_suffix("fsub", suffix, vl, f_1 - f_2);
                check_neon_suffix("st", "", vl, in_f(x));  // vector register is not used

                if (bits == 16) {
                    // Some of the math ops (exp,log,pow) for fp16 are converted into "xxx_fp32" call
                    // and then lowered to Internal::halide_xxx() function.
                    // In case the target has FP16 feature, native type conversion between fp16 and fp32 should be generated
                    // instead of emulated equivalent code with other types.
                    check_neon_suffix("fcvt", suffix, vl, exp(f_1));
                    check_neon_suffix("fcvt", suffix, vl, log(f_1));
                    check_neon_suffix("fcvt", suffix, vl, pow(f_1, f_2));
                }

                // No corresponding instructions exists for is_nan, is_inf, is_finite.
                // The instructions expected to be generated depends on CodeGen_LLVM::visit(const Call *op)
                check_neon_suffix(is_vector ? "fcmge" : "fcm", suffix, vl, is_nan(f_1));
                check_neon_suffix(is_vector ? "fabs" : "fneg", suffix, vl, is_inf(f_1));
                check_neon_suffix(is_vector ? "fcmlt" : "fcm", suffix, vl, is_finite(f_1));
            }

            for (int f : {2, 4}) {
                RDom r(0, f);
                const int vl = bits == 32 ? 4 : 8;
                const string suffix = bits == 32 ? ".4s" : ".8h";
                // A summation reduction that starts at something
                // non-trivial, to avoid llvm simplifying accumulating
                // widening summations into just widening summations.
                auto sum_ = [&](Expr e) {
                    Func f;
                    f(x) = cast(e.type(), 123);
                    f(x) += e;
                    return f(x);
                };
                // VPADD    I, F    -       Pairwise Add
                check_neon_suffix("faddp", suffix, vl, sum_(in_f(f * x + r)));
                // VPMAX    I, F    -       Pairwise Maximum
                check_neon_suffix("fmaxp", suffix, vl, maximum(in_f(f * x + r)));
                // VPMIN    I, F    -       Pairwise Minimum
                check_neon_suffix("fminp", suffix, vl, minimum(in_f(f * x + r)));
            }

            // VST2 X       -       Store two-element structures
            for (int width = 128; width <= 128 * 4; width *= 2) {
                const int vector_size = width / bits;
                Func tmp1, tmp2;
                tmp1(x) = cast(Float(bits), x);
                tmp1.compute_root();
                tmp2(x, y) = select(x % 2 == 0, tmp1(x / 2), tmp1(x / 2 + 16));
                tmp2.compute_root().vectorize(x, vector_size);
                auto suffix = suffix_of_st(2, bits, vector_size);
                check_neon_suffix("st2", suffix, vector_size, tmp2(0, 0) + tmp2(0, 127));
            }
            // Also check when the two expressions interleaved have a common
            // subexpression, which results in a vector var being lifted out.
            for (int width = 128; width <= 128 * 4; width *= 2) {
                const int vector_size = width / bits;
                Func tmp1, tmp2;
                tmp1(x) = cast(Float(bits), x);
                tmp1.compute_root();
                Expr e = (tmp1(x / 2) * 2 + 7) / 4;
                tmp2(x, y) = select(x % 2 == 0, e * 3, e + 17);
                tmp2.compute_root().vectorize(x, vector_size);
                auto suffix = suffix_of_st(2, bits, vector_size);
                check_neon_suffix("st2", suffix, vector_size, tmp2(0, 0) + tmp2(0, 127));
            }

            // VST3 X       -       Store three-element structures
            for (int width = 192; width <= 192 * 4; width *= 2) {
                const int vector_size = width / bits;
                Func tmp1, tmp2;
                tmp1(x) = cast(Float(bits), x);
                tmp1.compute_root();
                tmp2(x, y) = select(x % 3 == 0, tmp1(x / 3),
                                    x % 3 == 1, tmp1(x / 3 + 16),
                                    tmp1(x / 3 + 32));
                tmp2.compute_root().vectorize(x, vector_size);
                auto suffix = suffix_of_st(3, bits, vector_size);
                check_neon_suffix("st3", suffix, vector_size, tmp2(0, 0) + tmp2(0, 127));
            }

            // VST4 X       -       Store four-element structures
            for (int width = 256; width <= 256 * 4; width *= 2) {
                const int vector_size = width / bits;
                Func tmp1, tmp2;
                tmp1(x) = cast(Float(bits), x);
                tmp1.compute_root();
                tmp2(x, y) = select(x % 4 == 0, tmp1(x / 4),
                                    x % 4 == 1, tmp1(x / 4 + 16),
                                    x % 4 == 2, tmp1(x / 4 + 32),
                                    tmp1(x / 4 + 48));
                tmp2.compute_root().vectorize(x, vector_size);
                auto suffix = suffix_of_st(4, bits, vector_size);
                check_neon_suffix("st4", suffix, vector_size, tmp2(0, 0) + tmp2(0, 127));
            }
        }

        {
            // Actually, the following ops are not vectorized because SIMD instruction is unavailable.
            // The purpose of the test is just to confirm no error.
            // In case the target has FP16 feature, native type conversion between fp16 and fp32 should be generated
            // instead of emulated equivalent code with other types.
            auto check_native_conv = [&](const string &op, Expr e) {
                check_neon_suffix(op, "", 1, e);
                check_neon_suffix("fcvt", "h", 1, e);
            };
            check_native_conv("sinf", sin(f16_1));
            check_native_conv("asinf", asin(f16_1));
            check_native_conv("cosf", cos(f16_1));
            check_native_conv("acosf", acos(f16_1));
            check_native_conv("tanf", tan(f16_1));
            check_native_conv("atanf", atan(f16_1));
            check_native_conv("atan2f", atan2(f16_1, f16_2));
            check_native_conv("sinhf", sinh(f16_1));
            check_native_conv("asinhf", asinh(f16_1));
            check_native_conv("coshf", cosh(f16_1));
            check_native_conv("acoshf", acosh(f16_1));
            check_native_conv("tanhf", tanh(f16_1));
            check_native_conv("atanhf", atanh(f16_1));
        }
    }

private:
    void check_neon_suffix(string op, string suffix, int vector_width, Expr e) {
        // Filter out the test case
        if (!wildcard_match(filter, op)) return;

        // Register to tasks
        check(op, vector_width, e);

        // Store the corresponding suffix
        assert(tasks.size());
        assert(tasks.back().op == op);
        suffix_map.emplace(tasks.back().name, suffix);
    }

    void compile_and_check(Func error, const string &op, const string &name, int vector_width, std::ostringstream &error_msg) override {
        std::string fn_name = "test_" + name;
        std::string file_name = output_directory + fn_name;

        auto ext = Internal::get_output_info(target);
        std::map<OutputFileType, std::string> outputs = {
            {OutputFileType::c_header, file_name + ext.at(OutputFileType::c_header).extension},
            {OutputFileType::object, file_name + ext.at(OutputFileType::object).extension},
            {OutputFileType::assembly, file_name + ".s"},
        };
        error.compile_to(outputs, arg_types, fn_name, target);

        std::ifstream asm_file;
        asm_file.open(file_name + ".s");

        bool found_it = false;

        string suffix = suffix_map[name];
        std::ostringstream msg;
        msg << op << " did not generate for target=" << target.to_string()
            << " suffix=" << suffix
            << " vector_width=" << vector_width << ". Instead we got:\n";

        string line;
        while (getline(asm_file, line)) {
            msg << line << "\n";

            // Check for the op in question. In addition, check if the expected suffix exists in the operand
            found_it |= wildcard_search(op, line) && wildcard_search(suffix, line) && !wildcard_search("_" + op, line);
        }

        if (!found_it) {
            error_msg << "Failed: " << msg.str() << "\n";
        }

        asm_file.close();
    }

    string suffix_of_st(int num_elements, int bits, int vector_size) {
        assert(num_elements >= 2 && num_elements <= 4);
        assert(vector_size % num_elements == 0);
        const int num_lanes = vector_size / num_elements;
        switch (bits) {
        case 32:
            return num_lanes == 2 ? ".2s" : ".4s";
        case 16:
            return num_lanes == 4 ? ".4h" : ".8h";
        default:
            assert(0);
            return "unsupported_bits";
        }
    }

    std::map<string, string> suffix_map;
    const Var x{"x"}, y{"y"};
};
}  // namespace

int main(int argc, char **argv) {
    Target host = get_host_target();
    Target hl_target = get_target_from_environment();
    Target jit_target = get_jit_target_from_environment();
    printf("host is:      %s\n", host.to_string().c_str());
    printf("HL_TARGET is: %s\n", hl_target.to_string().c_str());
    printf("HL_JIT_TARGET is: %s\n", jit_target.to_string().c_str());

    // Only for 64bit target with fp16 feature
    if (!(hl_target.arch == Target::ARM && hl_target.bits == 64 && hl_target.has_feature(Target::ARMFp16))) {
        printf("[SKIP] To run this test, set HL_TARGET=arm-64-<os>-arm_fp16. \n");
        return 0;
    }
    // Create Test Object
    // Use smaller dimension than default(768, 128) to avoid fp16 overflow in reduction test case
    SimdOpCheck test(hl_target, 384, 32);

    if (!test.can_run_code()) {
        printf("[WARN] To run verification of realization, set HL_JIT_TARGET=arm-64-<os>-arm_fp16. \n");
    }

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
