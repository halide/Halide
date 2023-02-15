#include "Halide.h"
#include "simd_op_check.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

class SimdOpCheckXtensa : public SimdOpCheckTest {
public:
    SimdOpCheckXtensa(Target t, int w = 768 /*256*3*/, int h = 128)
        : SimdOpCheckTest(t, w, h) {
    }
    void setup_images() override {
        for (auto p : image_params) {
            p.reset();
        }
    }

    bool can_run_code() const override {
        return false;
    }

    void compile_and_check(Func error, const std::string &op, const std::string &name, int vector_width, std::ostringstream &error_msg) override {
        // Compile just the vector Func to assembly.
        std::string cpp_filename = output_directory + "check_" + name + ".cpp";
        error.compile_to_c(cpp_filename, arg_types, "", target);
        std::ifstream cpp_file;
        cpp_file.open(cpp_filename);

        bool found_it = false;

        std::ostringstream msg;
        msg << op << " did not generate for target=" << target.to_string() << " vector_width=" << vector_width << ". Instead we got:\n";

        std::string line;
        // We are going to print only main function.
        msg << "Skipping non-main function definitions..."
            << "\n";
        std::string sanitized_op = sanitize(op);
        bool inside_the_function = false;
        while (getline(cpp_file, line)) {
            if (!inside_the_function && ((line.find("int _op_" + op) != std::string::npos) || (line.find("int _op_" + sanitized_op) != std::string::npos))) {
                inside_the_function = true;
            }
            if (!inside_the_function) {
                continue;
            }

            msg << line << "\n";
            // Check for the op in question
            found_it |= wildcard_search(op, line) && !wildcard_search("_" + op, line);
        }

        if (!found_it) {
            error_msg << "Failed: " << msg.str() << "\n";
        }

        cpp_file.close();

        // Also compile the error checking Func (to be sure it compiles without error)
        std::string fn_name = "test_" + name;
        std::string fn_cpp_name = fn_name + ".cpp";
        std::string fn_h_name = fn_name + +".h";
        error.compile_to_c(output_directory + fn_cpp_name, arg_types, fn_name, target);
        error.compile_to_header(output_directory + fn_h_name, arg_types, fn_name, target);
    }

    void add_tests() override {
        Expr f16_1 = cast<Halide::float16_t>(in_f16(x));
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x + 16), f32_3 = in_f32(x + 32);
        Expr f64_1 = in_f64(x), f64_2 = in_f64(x + 16), f64_3 = in_f64(x + 32);
        Expr i8_1 = in_i8(x), i8_2 = in_i8(x + 16), i8_3 = in_i8(x + 32), i8_4 = in_i8(x + 48);
        Expr u8_1 = in_u8(x), u8_2 = in_u8(x + 16), u8_3 = in_u8(x + 32), u8_4 = in_u8(x + 48);
        Expr u8_even = in_u8(2 * x), u8_odd = in_u8(2 * x + 1);
        Expr i16_1 = in_i16(x), i16_2 = in_i16(x + 16), i16_3 = in_i16(x + 32), i16_4 = in_i16(x + 48);
        Expr u16_1 = in_u16(x), u16_2 = in_u16(x + 16), u16_3 = in_u16(x + 32), u16_4 = in_u16(x + 48);
        Expr i32_1 = in_i32(x), i32_2 = in_i32(x + 16), i32_3 = in_i32(x + 32);
        Expr u32_1 = in_u32(x), u32_2 = in_u32(x + 16), u32_3 = in_u32(x + 32);
        Expr i64_1 = in_i64(x), i64_2 = in_i64(x + 16), i64_3 = in_i64(x + 32);
        Expr u64_1 = in_u64(x), u64_2 = in_u64(x + 16), u64_3 = in_u64(x + 32);
        Expr bool_1 = (f32_1 > 0.3f), bool_2 = (f32_1 < -0.3f), bool_3 = (f32_1 != -0.34f);

        int vector_width = 64;

        // 48-bit math
        check("IVP_MULNX16", vector_width / 2, i32(i16_1) * i32(i16_2));
        check("IVP_MULUUNX16", vector_width / 2, u32(u16_1) * u32(u16_2));
        // TODO(aelphy): fails to compile due to poor support of int48_t
        // check("halide_xtensa_widen_pair_mul_i48", vector_width / 2, i48(i16_1) * i48(i16_2) + i48(i16_3) * i48(i16_4));
        check("IVP_MULUUNX16", vector_width / 2, u32(u16_1) * u32(u16_2) + u32(u16_3) * u32(u16_4));
        // check("IVP_MULUUPNX16", vector_width / 2, i48(u16_1) * i48(u16_2) + i48(u16_3) * i48(u16_4));

        // check("halide_xtensa_widen_add_i48", vector_width / 2, i32(i16_1) + i32(i16_2));
        // check("halide_xtensa_widen_add_u48", vector_width / 2, u32(u16_1) + u32(u16_2));

        // Multiplications.
        check("IVP_MULNX16PACKL", vector_width / 2, i16_1 * i16_2);
        check("IVP_MULN_2X32", vector_width / 2, i32_1 * i32_2);

        // Shifts.
        check("IVP_SRLNX16", vector_width / 2, u16_1 >> u16_2);
        check("IVP_SRLINX16U", vector_width / 2, u16_1 / 4);
        check("IVP_SRLN_2X32", vector_width / 4, u32_1 >> u32_2);
        check("IVP_SRLIN_2X32", vector_width / 4, u32_1 / 4);
        check("IVP_SLLNX16U", vector_width / 2, u16_1 << u16_2);
        check("IVP_SLLINX16U", vector_width / 2, u16_1 * 4);
        check("IVP_SLLN_2X32", vector_width / 4, u32_1 << u32_2);
        check("IVP_SLLIN_2X32", vector_width / 4, u32_1 * 4);

        // Casts.
        check("convert<int32x32_t,int16x32_t>", vector_width / 2, i32(i16_1));
        check("convert<float16x32_t,float32x32_t>", vector_width / 2, f16(f32_1));
        check("convert<float32x32_t, float16x32_t>", vector_width / 2, f32(f16_1));
        check("convert<float32x32_t, int16x32_t>", vector_width / 2, f32(i16_1));
        check("convert<float32x32_t, uint16x32_t>", vector_width / 2, f32(u16_1));
        check("convert<uint32x32_t, uint16x32_t>", vector_width / 2, u32(u16_1));
        check("store_narrowing<int32x16_t, int16_t, 16>", vector_width / 4, i16(i32_1));
        check("store_narrowing<uint32x16_t, uint16_t, 16>", vector_width / 4, u16(u32_1));
        check("store_narrowing<int16x32_t, int8_t, 32>", vector_width / 2, i8(i16_1));
        check("store_narrowing<uint16x32_t, uint8_t, 32>", vector_width / 2, u8(u16_1));

        // Averaging instructions.
        check("IVP_AVGUNX16", vector_width / 2, u16((u32(u16_1) + u32(u16_2)) / 2));
        check("IVP_AVGNX16", vector_width / 2, i16((i32(i16_1) + i32(i16_2)) / 2));
        check("IVP_AVGRUNX16", vector_width / 2, u16((u32(u16_1) + u32(u16_2) + 1) / 2));
        check("IVP_AVGRNX16", vector_width / 2, i16((i32(i16_1) + i32(i16_2) + 1) / 2));

        // Saturating arithmetic
        check("IVP_ADDSNX16", vector_width / 2, i16_sat(i32(i16_1) + i32(i16_2)));
        check("halide_xtensa_sat_add_i32", vector_width / 4, i32_sat(i64(i32_1) + i64(i32_2)));
        check("IVP_SUBSNX16", vector_width / 2, i16_sat(i32(i16_1) - i32(i16_2)));
        check("IVP_ABSSUBNX16", vector_width / 2, absd(u16_1, u16_2));
        check("IVP_ABSSUBNX16", vector_width / 2, absd(i16_1, i16_2));

        // Min/max
        check("IVP_MAXUNX16", vector_width / 2, max(u16_1, u16_2));
        check("IVP_MAXNX16", vector_width / 2, max(i16_1, i16_2));
        check("IVP_MINUNX16", vector_width / 2, min(u16_1, u16_2));
        check("IVP_MINNX16", vector_width / 2, min(i16_1, i16_2));
        check("IVP_MAXUN_2X32", vector_width / 4, max(u32_1, u32_2));
        check("IVP_MAXN_2X32", vector_width / 4, max(i32_1, i32_2));
        check("IVP_MINUN_2X32", vector_width / 4, min(u32_1, u32_2));
        check("IVP_MINN_2X32", vector_width / 4, min(i32_1, i32_2));

        // Count_leading_zeros
        check("IVP_NSAUNX16", vector_width / 2, count_leading_zeros(u16_1));
        check("IVP_NSAUNX16", vector_width / 2, count_leading_zeros(i16_1));
        check("IVP_NSAUN_2X32", vector_width / 4, count_leading_zeros(u32_1));
        check("IVP_NSAUN_2X32", vector_width / 4, count_leading_zeros(i32_1));

        //  Shifts
        check("IVP_PACKVRNRNX48", vector_width / 2, i16(widening_mul(i16_1, i16_2) >> 4));

        // These are not generated right now, because vectors are split now, so comment out for now.
        // Narrowing with shifting.
        // check("halide_xtensa_narrow_with_shift_i16", vector_width / 2, i16(i32_1 >> i32_2));
        check("halide_xtensa_narrow_with_shift_i16", vector_width / 2, i16(i32_1 / 4));
        // check("halide_xtensa_narrow_with_shift_u16", vector_width / 2, u16(i32_1 >> i32_2));
        check("halide_xtensa_narrow_with_shift_u16", vector_width / 2, u16(i32_1 / 4));

        check("IVP_AVGRNX16", vector_width / 2, i16((i32(i16_1) + i32(i16_2) + 1) / 2));
    }

private:
    const Var x{"x"}, y{"y"};
};

int main(int argc, char **argv) {
    Target host = get_host_target();
    Target hl_target = get_target_from_environment();
    printf("host is:      %s\n", host.to_string().c_str());
    printf("HL_TARGET is: %s\n", hl_target.to_string().c_str());

    if (!hl_target.has_feature(Target::Xtensa)) {
        printf("[SKIP] Skipping the simd_op_check_xtensa test, because target doesn't have xtensa feature flag enabled\n");
        return 0;
    }
    SimdOpCheckXtensa test_xtensa(hl_target);

    if (argc > 1) {
        test_xtensa.filter = argv[1];
    }

    if (argc > 2) {
        // Don't forget: if you want to run the standard tests to a specific output
        // directory, you'll need to invoke with the first arg enclosed
        // in quotes (to avoid it being wildcard-expanded by the shell):
        //
        //    correctness_simd_op_check "*" /path/to/output
        //
        test_xtensa.output_directory = argv[2];
    }
    bool success = test_xtensa.test_all();

    // Compile a runtime for this target, for use in the static test.
    // TODO(vksnk): that's going to be different for xtensa?
    compile_standalone_runtime(test_xtensa.output_directory + "simd_op_check_runtime.o", test_xtensa.target);

    if (!success) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
