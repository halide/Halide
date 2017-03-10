#include "Halide.h"

#include <fstream>
#include <future>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "test/common/halide_test_dirs.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

// This tests that we can correctly generate all the simd ops
using std::vector;
using std::string;

// width and height of test images
constexpr int W = 256*3;
constexpr int H = 128;

constexpr int max_i8  = 127;
constexpr int max_i16 = 32767;
constexpr int max_i32 = 0x7fffffff;
constexpr int max_u8  = 255;
constexpr int max_u16 = 65535;
const Expr max_u32 = UInt(32).max();

const Var x{"x"}, y{"y"};

struct TestResult {
    string op;
    string error_msg;
};

struct Task {
    string op;
    string name;
    int vector_width;
    Expr expr;
};

size_t num_threads = Halide::Internal::ThreadPool<void>::num_processors_online();

struct Test {
    bool use_avx2{false};
    bool use_avx512{false};
    bool use_avx512_cannonlake{false};
    bool use_avx512_knl{false};
    bool use_avx512_skylake{false};
    bool use_avx{false};
    bool use_power_arch_2_07{false};
    bool use_sse41{false};
    bool use_sse42{false};
    bool use_ssse3{false};
    bool use_vsx{false};

    string filter{"*"};
    string output_directory{Internal::get_test_tmp_dir()};
    vector<Task> tasks;

    Target target;

    ImageParam in_f32{Float(32), 1, "in_f32"};
    ImageParam in_f64{Float(64), 1, "in_f64"};
    ImageParam in_i8 {Int(8), 1, "in_i8"};
    ImageParam in_u8 {UInt(8), 1, "in_u8"};
    ImageParam in_i16{Int(16), 1, "in_i16"};
    ImageParam in_u16{UInt(16), 1, "in_u16"};
    ImageParam in_i32{Int(32), 1, "in_i32"};
    ImageParam in_u32{UInt(32), 1, "in_u32"};
    ImageParam in_i64{Int(64), 1, "in_i64"};
    ImageParam in_u64{UInt(64), 1, "in_u64"};

    const vector<ImageParam> image_params{in_f32, in_f64, in_i8, in_u8, in_i16, in_u16, in_i32, in_u32, in_i64, in_u64};
    const vector<Argument> arg_types{in_f32, in_f64, in_i8, in_u8, in_i16, in_u16, in_i32, in_u32, in_i64, in_u64};

    Test() {
        target = get_target_from_environment()
            .with_feature(Target::NoBoundsQuery)
            .with_feature(Target::NoAsserts)
            .with_feature(Target::NoRuntime);
        use_avx512_knl = target.has_feature(Target::AVX512_KNL);
        use_avx512_cannonlake = target.has_feature(Target::AVX512_Cannonlake);
        use_avx512_skylake = use_avx512_cannonlake || target.has_feature(Target::AVX512_Skylake);
        use_avx512 = use_avx512_knl || use_avx512_skylake || use_avx512_cannonlake || target.has_feature(Target::AVX512);
        use_avx2 = use_avx512 || target.has_feature(Target::AVX2);
        use_avx = use_avx2 || target.has_feature(Target::AVX);
        use_sse41 = use_avx || target.has_feature(Target::SSE41);

        // There's no separate target for SSSE3; we currently enable it in
        // lockstep with SSE4.1
        use_ssse3 = use_sse41;
        // There's no separate target for SSS4.2; we currently assume that
        // it should be used iff AVX is being used.
        use_sse42 = use_avx;

        use_vsx = target.has_feature(Target::VSX);
        use_power_arch_2_07 = target.has_feature(Target::POWER_ARCH_2_07);

        // We are going to call realize, i.e. we are going to JIT code.
        // Not all platforms support JITting. One indirect yet quick
        // way of identifying this is to see if we can run code on the
        // host. This check is in no ways really a complete check, but
        // it works for now.
        const bool can_run = can_run_code();
        for (auto p : image_params) {
            p.set_host_alignment(128);
            p.dim(0).set_min(0);
            if (can_run) {
                // Make a buffer filled with noise to use as a sample input.
                Buffer<> b(p.type(), {W*4+H, H});
                Expr r;
                if (p.type().is_float()) {
                    r = cast(p.type(), random_float() * 1024 - 512);
                } else {
                    // Avoid cases where vector vs scalar do different things
                    // on signed integer overflow by limiting ourselves to 28
                    // bit numbers.
                    r = cast(p.type(), random_int() / 4);
                }
                lambda(x, y, r).realize(b);
                p.set(b);
            }
        }
    }

    bool can_run_code() const {
        // If we can (target matches host), run the error checking Func.
        Target host_target = get_host_target();
        bool can_run_the_code =
            (target.arch == host_target.arch &&
             target.bits == host_target.bits &&
             target.os == host_target.os);
        // A bunch of feature flags also need to match between the
        // compiled code and the host in order to run the code.
        for (Target::Feature f : {Target::SSE41, Target::AVX,
                    Target::AVX2, Target::AVX512,
                    Target::FMA, Target::FMA4, Target::F16C,
                    Target::VSX, Target::POWER_ARCH_2_07,
                    Target::ARMv7s, Target::NoNEON, Target::MinGW}) {
            if (target.has_feature(f) != host_target.has_feature(f)) {
                can_run_the_code = false;
            }
        }
        return can_run_the_code;
    }

    // Check if pattern p matches str, allowing for wildcards (*).
    bool wildcard_match(const char* p, const char* str) const {
        // Match all non-wildcard characters.
        while (*p && *str && *p == *str && *p != '*') {
            str++;
            p++;
        }

        if (!*p) {
            return *str == 0;
        } else if (*p == '*') {
            p++;
            do {
                if (wildcard_match(p, str)) {
                    return true;
                }
            } while(*str++);
        } else if (*p == ' ') {     // ignore whitespace in pattern
            p++;
            if (wildcard_match(p, str)) {
                return true;
            }
        } else if (*str == ' ') {   // ignore whitespace in string
            str++;
            if (wildcard_match(p, str)) {
                return true;
            }
        }
        return !*p;
    }

    bool wildcard_match(const string& p, const string& str) const {
        return wildcard_match(p.c_str(), str.c_str());
    }

    // Check if a substring of str matches a pattern p.
    bool wildcard_search(const string& p, const string& str) const {
        return wildcard_match("*" + p + "*", str);
    }

    TestResult check_one(const string &op, const string &name, int vector_width, Expr e) const {
        std::ostringstream error_msg;

        // Define a vectorized Func that uses the pattern.
        Func f(name);
        f(x, y) = e;
        f.bound(x, 0, W).vectorize(x, vector_width);
        f.compute_root();

        // Include a scalar version
        Func f_scalar("scalar_" + name);
        f_scalar(x, y) = e;
        f_scalar.bound(x, 0, W);
        f_scalar.compute_root();

        // The output to the pipeline is the maximum absolute difference as a double.
        RDom r(0, W, 0, H);
        Func error("error_" + name);
        error() = cast<double>(maximum(absd(f(r.x, r.y), f_scalar(r.x, r.y))));

        {
            // Compile just the vector Func to assembly.
            string asm_filename = output_directory + "check_" + name + ".s";
            f.compile_to_assembly(asm_filename, arg_types, target);

            std::ifstream asm_file;
            asm_file.open(asm_filename);

            bool found_it = false;

            std::ostringstream msg;
            msg << op << " did not generate. Instead we got:\n";

            string line;
            while (getline(asm_file, line)) {
                msg << line << "\n";

                // Check for the op in question
                found_it |= wildcard_search(op, line) && !wildcard_search("_" + op, line);
            }

            if (!found_it) {
                error_msg << "Failed: " << msg.str() << "\n";
            }

            asm_file.close();
        }

        // Also compile the error checking Func (to be sure it compiles without error)
        string fn_name = "test_" + name;
        error.compile_to_file(output_directory + fn_name, arg_types, fn_name, target);

        bool can_run_the_code = can_run_code();
        if (can_run_the_code) {
            Realization r = error.realize(target.without_feature(Target::NoRuntime));
            double e = Buffer<double>(r[0])();
            // Use a very loose tolerance for floating point tests. The
            // kinds of bugs we're looking for are codegen bugs that
            // return the wrong value entirely, not floating point
            // accuracy differences between vectors and scalars.
            if (e > 0.001) {
                error_msg << "The vector and scalar versions of " << name << " disagree. Maximum error: " << e << "\n";
            }
        }

        return { op, error_msg.str() };
    }

    void check(string op, int vector_width, Expr e) {
        // Make a name for the test by uniquing then sanitizing the op name
        string name = "op_" + op;
        for (size_t i = 0; i < name.size(); i++) {
            if (!isalnum(name[i])) name[i] = '_';
        }

        name += "_" + std::to_string(tasks.size());

        // Bail out after generating the unique_name, so that names are
        // unique across different processes and don't depend on filter
        // settings.
        if (!wildcard_match(filter, op)) return;

        tasks.emplace_back(Task {op, name, vector_width, e});
    }

    void check_sse_all() {
        #if LLVM_VERSION > 39
        #define YMM "*ymm"
        #else
        #define YMM
        #endif

        Expr f64_1 = in_f64(x), f64_2 = in_f64(x+16), f64_3 = in_f64(x+32);
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x+16), f32_3 = in_f32(x+32);
        Expr i8_1  = in_i8(x),  i8_2  = in_i8(x+16),  i8_3  = in_i8(x+32);
        Expr u8_1  = in_u8(x),  u8_2  = in_u8(x+16),  u8_3  = in_u8(x+32);
        Expr i16_1 = in_i16(x), i16_2 = in_i16(x+16), i16_3 = in_i16(x+32);
        Expr u16_1 = in_u16(x), u16_2 = in_u16(x+16), u16_3 = in_u16(x+32);
        Expr i32_1 = in_i32(x), i32_2 = in_i32(x+16), i32_3 = in_i32(x+32);
        Expr u32_1 = in_u32(x), u32_2 = in_u32(x+16), u32_3 = in_u32(x+32);
        Expr i64_1 = in_i64(x), i64_2 = in_i64(x+16), i64_3 = in_i64(x+32);
        Expr u64_1 = in_u64(x), u64_2 = in_u64(x+16), u64_3 = in_u64(x+32);
        Expr bool_1 = (f32_1 > 0.3f), bool_2 = (f32_1 < -0.3f), bool_3 = (f32_1 != -0.34f);

        // MMX and SSE1 (in 64 and 128 bits)
        for (int w = 1; w <= 4; w++) {
            // LLVM promotes these to wider types for 64-bit vectors,
            // which is probably fine. Often you're 64-bits wide because
            // you're about to upcast, and using the wider types makes the
            // upcast cheap.
            if (w > 1) {
                check("paddb",   8*w, u8_1 + u8_2);
                check("psubb",   8*w, u8_1 - u8_2);
                check("paddw",   4*w, u16_1 + u16_2);
                check("psubw",   4*w, u16_1 - u16_2);
                check("pmullw",  4*w, i16_1 * i16_2);
                check("paddd",   2*w, i32_1 + i32_2);
                check("psubd",   2*w, i32_1 - i32_2);
            }

            check("paddsb",  8*w, i8_sat(i16(i8_1) + i16(i8_2)));
            // Add a test with a constant as there was a bug on this.
            check("paddsb",  8*w, i8_sat(i16(i8_1) + i16(3)));
            check("psubsb",  8*w, i8_sat(i16(i8_1) - i16(i8_2)));
            check("paddusb", 8*w, u8(min(u16(u8_1) + u16(u8_2), max_u8)));
            check("psubusb", 8*w, u8(max(i16(u8_1) - i16(u8_2), 0)));

            check("paddsw",  4*w, i16_sat(i32(i16_1) + i32(i16_2)));
            check("psubsw",  4*w, i16_sat(i32(i16_1) - i32(i16_2)));
            check("paddusw", 4*w, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
            check("psubusw", 4*w, u16(max(i32(u16_1) - i32(u16_2), 0)));
            check("pmulhw",  4*w, i16((i32(i16_1) * i32(i16_2)) / (256*256)));
            check("pmulhw",  4*w, i16((i32(i16_1) * i32(i16_2)) >> 16));

            // Add a test with a constant as there was a bug on this.
            check("pmulhw",  4*w, i16((3 * i32(i16_2)) / (256*256)));

            // There was a bug with this case too. CSE was lifting out the
            // information that made it possible to do the narrowing.
            check("pmulhw",  4*w, select(in_u8(0) == 0,
                                      i16((3 * i32(i16_2)) / (256*256)),
                                      i16((5 * i32(i16_2)) / (256*256))));

            check("pmulhuw", 4*w, i16_1 / 15);


            check("pcmp*b", 8*w, select(u8_1 == u8_2, u8(1), u8(2)));
            check("pcmp*b", 8*w, select(u8_1 > u8_2, u8(1), u8(2)));
            check("pcmp*w", 4*w, select(u16_1 == u16_2, u16(1), u16(2)));
            check("pcmp*w", 4*w, select(u16_1 > u16_2, u16(1), u16(2)));
            check("pcmp*d", 2*w, select(u32_1 == u32_2, u32(1), u32(2)));
            check("pcmp*d", 2*w, select(u32_1 > u32_2, u32(1), u32(2)));

            // SSE 1
            check("addps", 2*w, f32_1 + f32_2);
            check("subps", 2*w, f32_1 - f32_2);
            check("mulps", 2*w, f32_1 * f32_2);

            // Padding out the lanes of a div isn't necessarily a good
            // idea, and so llvm doesn't do it.
            if (w > 1) {
                // LLVM no longer generates division instructions with
                // fast-math on (instead it uses the approximate
                // reciprocal, a newtown rhapson step, and a
                // multiplication by the numerator).
                //check("divps", 2*w, f32_1 / f32_2);
            }

            check(use_avx512_skylake ? "vrsqrt14ps" : "rsqrtps", 2*w, fast_inverse_sqrt(f32_1));
            check(use_avx512_skylake ? "vrcp14ps" : "rcpps", 2*w, fast_inverse(f32_1));
            check("sqrtps", 2*w, sqrt(f32_2));
            check("maxps", 2*w, max(f32_1, f32_2));
            check("minps", 2*w, min(f32_1, f32_2));
            check("pavgb", 8*w, u8((u16(u8_1) + u16(u8_2) + 1)/2));
            check("pavgb", 8*w, u8((u16(u8_1) + u16(u8_2) + 1)>>1));
            check("pavgw", 4*w, u16((u32(u16_1) + u32(u16_2) + 1)/2));
            check("pavgw", 4*w, u16((u32(u16_1) + u32(u16_2) + 1)>>1));
            check("pmaxsw", 4*w, max(i16_1, i16_2));
            check("pminsw", 4*w, min(i16_1, i16_2));
            check("pmaxub", 8*w, max(u8_1, u8_2));
            check("pminub", 8*w, min(u8_1, u8_2));
            check("pmulhuw", 4*w, u16((u32(u16_1) * u32(u16_2))/(256*256)));
            check("pmulhuw", 4*w, u16((u32(u16_1) * u32(u16_2))>>16));
            check("pmulhuw", 4*w, u16_1 / 15);


            check("cmpeqps", 2*w, select(f32_1 == f32_2, 1.0f, 2.0f));
            check("cmpltps", 2*w, select(f32_1 < f32_2, 1.0f, 2.0f));

            // These get normalized to not of eq, and not of lt with the args flipped
            //check("cmpneqps", 2*w, cast<int32_t>(f32_1 != f32_2));
            //check("cmpleps", 2*w, cast<int32_t>(f32_1 <= f32_2));

        }

        // These guys get normalized to the integer versions for widths
        // other than 128-bits. Avx512 has mask-register versions.
        // check("andnps", 4, bool_1 & (~bool_2));
        check(use_avx512_skylake ? "korw" : "orps", 4, bool_1 | bool_2);
        check(use_avx512_skylake ? "kxorw" : "xorps", 4, bool_1 ^ bool_2);
        if (!use_avx512) {
            // avx512 implicitly ands the predicates by masking the second
            // comparison using the result of the first. Clever!
            check("andps", 4, bool_1 & bool_2);
        }


        // These ones are not necessary, because we just flip the args and cmpltps or cmpleps
        //check("cmpnleps", 4, select(f32_1 > f32_2, 1.0f, 2.0f));
        //check("cmpnltps", 4, select(f32_1 >= f32_2, 1.0f, 2.0f));

        check("shufps", 4, in_f32(2*x));

        // SSE 2

        for (int w = 2; w <= 4; w++) {
            check("addpd", w, f64_1 + f64_2);
            check("subpd", w, f64_1 - f64_2);
            check("mulpd", w, f64_1 * f64_2);
            check("divpd", w, f64_1 / f64_2);
            check("sqrtpd", w, sqrt(f64_2));
            check("maxpd", w, max(f64_1, f64_2));
            check("minpd", w, min(f64_1, f64_2));

            check("cmpeqpd", w, select(f64_1 == f64_2, 1.0f, 2.0f));
            //check("cmpneqpd", w, select(f64_1 != f64_2, 1.0f, 2.0f));
            //check("cmplepd", w, select(f64_1 <= f64_2, 1.0f, 2.0f));
            check("cmpltpd", w, select(f64_1 < f64_2, 1.0f, 2.0f));

            // llvm is pretty inconsistent about which ops get generated
            // for casts. We don't intend to catch these for now, so skip
            // them.

            //check("cvttpd2dq", 4, i32(f64_1));
            //check("cvtdq2pd", 4, f64(i32_1));
            //check("cvttps2dq", 4, i32(f32_1));
            //check("cvtdq2ps", 4, f32(i32_1));
            //check("cvtps2pd", 4, f64(f32_1));
            //check("cvtpd2ps", 4, f32(f64_1));

            check("paddq", w, i64_1 + i64_2);
            check("psubq", w, i64_1 - i64_2);
            check(use_avx512_skylake ? "vpmullq" : "pmuludq", w, u64_1 * u64_2);

            check("packssdw", 4*w, i16_sat(i32_1));
            check("packsswb", 8*w, i8_sat(i16_1));
            check("packuswb", 8*w, u8_sat(i16_1));
        }

        // SSE 3

        // We don't do horizontal add/sub ops, so nothing new here

        // SSSE 3
        if (use_ssse3) {
            for (int w = 2; w <= 4; w++) {
                check("pabsb", 8*w, abs(i8_1));
                check("pabsw", 4*w, abs(i16_1));
                check("pabsd", 2*w, abs(i32_1));
            }
        }

        // SSE 4.1

        // skip dot product and argmin
        for (int w = 2; w <= 4; w++) {
            check("pmaddwd", 2*w, i32(i16_1) * 3 + i32(i16_2) * 4);
            check("pmaddwd", 2*w, i32(i16_1) * 3 - i32(i16_2) * 4);
        }

        if (use_avx2) {
            check("vpmaddwd", 8, i32(i16_1) * 3 + i32(i16_2) * 4);
        } else {
            check("pmaddwd", 8, i32(i16_1) * 3 + i32(i16_2) * 4);
        }

        // llvm doesn't distinguish between signed and unsigned multiplies
        //check("pmuldq", 4, i64(i32_1) * i64(i32_2));

        if (use_sse41) {
            for (int w = 2; w <= 4; w++) {
                if (!use_avx512) {
                    check("pmuludq", 2*w, u64(u32_1) * u64(u32_2));
                }
                check("pmulld", 2*w, i32_1 * i32_2);

                check((use_avx512_skylake && w > 2) ? "vinsertf32x8" : "blend*ps", 2*w, select(f32_1 > 0.7f, f32_1, f32_2));
                check((use_avx512 && w > 2) ? "vinsertf64x4" : "blend*pd", w, select(f64_1 > cast<double>(0.7f), f64_1, f64_2));
                check("pblend*b", 8*w, select(u8_1 > 7, u8_1, u8_2));
                check("pblend*b", 8*w, select(u8_1 == 7, u8_1, u8_2));
                check("pblend*b", 8*w, select(u8_1 <= 7, i8_1, i8_2));

                check("pmaxsb", 8*w, max(i8_1, i8_2));
                check("pminsb", 8*w, min(i8_1, i8_2));
                check("pmaxuw", 4*w, max(u16_1, u16_2));
                check("pminuw", 4*w, min(u16_1, u16_2));
                check("pmaxud", 2*w, max(u32_1, u32_2));
                check("pminud", 2*w, min(u32_1, u32_2));
                check("pmaxsd", 2*w, max(i32_1, i32_2));
                check("pminsd", 2*w, min(i32_1, i32_2));

                check("roundps", 2*w, round(f32_1));
                check("roundpd", w, round(f64_1));
                check("roundps", 2*w, floor(f32_1));
                check("roundpd", w, floor(f64_1));
                check("roundps", 2*w, ceil(f32_1));
                check("roundpd", w, ceil(f64_1));

                check("pcmpeqq", w, select(i64_1 == i64_2, i64(1), i64(2)));
                check("packusdw", 4*w, u16_sat(i32_1));
            }
        }

        // SSE 4.2
        if (use_sse42) {
            check("pcmpgtq", 2, select(i64_1 > i64_2, i64(1), i64(2)));
        }

        // AVX
        if (use_avx) {
            check("vsqrtps" YMM, 8, sqrt(f32_1));
            check("vsqrtpd" YMM, 4, sqrt(f64_1));
            check(use_avx512_skylake ? "vrsqrt14ps" : "vrsqrtps" YMM, 8, fast_inverse_sqrt(f32_1));
            check(use_avx512_skylake ? "vrcp14ps" : "vrcpps" YMM, 8, fast_inverse(f32_1));

#if 0
            // Not implemented in the front end.
            check("vandnps", 8, bool1 & (!bool2));
            check("vandps", 8, bool1 & bool2);
            check("vorps", 8, bool1 | bool2);
            check("vxorps", 8, bool1 ^ bool2);
#endif

            check("vaddps" YMM, 8, f32_1 + f32_2);
            check("vaddpd" YMM, 4, f64_1 + f64_2);
            check("vmulps" YMM, 8, f32_1 * f32_2);
            check("vmulpd" YMM, 4, f64_1 * f64_2);
            check("vsubps" YMM, 8, f32_1 - f32_2);
            check("vsubpd" YMM, 4, f64_1 - f64_2);
            // LLVM no longer generates division instruction when fast-math is on
            //check("vdivps", 8, f32_1 / f32_2);
            //check("vdivpd", 4, f64_1 / f64_2);
            check("vminps" YMM, 8, min(f32_1, f32_2));
            check("vminpd" YMM, 4, min(f64_1, f64_2));
            check("vmaxps" YMM, 8, max(f32_1, f32_2));
            check("vmaxpd" YMM, 4, max(f64_1, f64_2));
            check("vroundps" YMM, 8, round(f32_1));
            check("vroundpd" YMM, 4, round(f64_1));

            check("vcmpeqpd" YMM, 4, select(f64_1 == f64_2, 1.0f, 2.0f));
            //check("vcmpneqpd", 4, select(f64_1 != f64_2, 1.0f, 2.0f));
            //check("vcmplepd", 4, select(f64_1 <= f64_2, 1.0f, 2.0f));
            check("vcmpltpd" YMM, 4, select(f64_1 < f64_2, 1.0f, 2.0f));
            check("vcmpeqps" YMM, 8, select(f32_1 == f32_2, 1.0f, 2.0f));
            //check("vcmpneqps", 8, select(f32_1 != f32_2, 1.0f, 2.0f));
            //check("vcmpleps", 8, select(f32_1 <= f32_2, 1.0f, 2.0f));
            check("vcmpltps" YMM, 8, select(f32_1 < f32_2, 1.0f, 2.0f));

            // avx512 can do predicated insert ops instead of blends
            check(use_avx512_skylake ? "vinsertf32x8" : "vblend*ps" YMM, 8, select(f32_1 > 0.7f, f32_1, f32_2));
            check(use_avx512 ? "vinsertf64x4" : "vblend*pd" YMM, 4, select(f64_1 > cast<double>(0.7f), f64_1, f64_2));

            check("vcvttps2dq" YMM, 8, i32(f32_1));
            check("vcvtdq2ps" YMM, 8, f32(i32_1));
            check("vcvttpd2dqy", 8, i32(f64_1));
            check("vcvtdq2pd" YMM, 8, f64(i32_1));
            check("vcvtps2pd" YMM, 8, f64(f32_1));
            check("vcvtpd2psy", 8, f32(f64_1));

            // Newer llvms will just vpshufd straight from memory for reversed loads
            // check("vperm", 8, in_f32(100-x));
        }

        // AVX 2

        if (use_avx2) {
            check("vpaddb" YMM, 32, u8_1 + u8_2);
            check("vpsubb" YMM, 32, u8_1 - u8_2);
            check("vpaddsb", 32, i8_sat(i16(i8_1) + i16(i8_2)));
            check("vpsubsb", 32, i8_sat(i16(i8_1) - i16(i8_2)));
            check("vpaddusb", 32, u8(min(u16(u8_1) + u16(u8_2), max_u8)));
            check("vpsubusb", 32, u8(max(i16(u8_1) - i16(u8_2), 0)));
            check("vpaddw" YMM, 16, u16_1 + u16_2);
            check("vpsubw" YMM, 16, u16_1 - u16_2);
            check("vpaddsw", 16, i16_sat(i32(i16_1) + i32(i16_2)));
            check("vpsubsw", 16, i16_sat(i32(i16_1) - i32(i16_2)));
            check("vpaddusw", 16, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
            check("vpsubusw", 16, u16(max(i32(u16_1) - i32(u16_2), 0)));
            check("vpaddd" YMM, 8, i32_1 + i32_2);
            check("vpsubd" YMM, 8, i32_1 - i32_2);
            check("vpmulhw" YMM, 16, i16((i32(i16_1) * i32(i16_2)) / (256*256)));
            check("vpmulhw" YMM, 16, i16((i32(i16_1) * i32(i16_2)) >> 16));
            check("vpmullw" YMM, 16, i16_1 * i16_2);

            check("vpcmp*b" YMM, 32, select(u8_1 == u8_2, u8(1), u8(2)));
            check("vpcmp*b" YMM, 32, select(u8_1 > u8_2, u8(1), u8(2)));
            check("vpcmp*w" YMM, 16, select(u16_1 == u16_2, u16(1), u16(2)));
            check("vpcmp*w" YMM, 16, select(u16_1 > u16_2, u16(1), u16(2)));
            check("vpcmp*d" YMM, 8, select(u32_1 == u32_2, u32(1), u32(2)));
            check("vpcmp*d" YMM, 8, select(u32_1 > u32_2, u32(1), u32(2)));

            check("vpavgb", 32, u8((u16(u8_1) + u16(u8_2) + 1)/2));
            check("vpavgw", 16, u16((u32(u16_1) + u32(u16_2) + 1)/2));
            check("vpmaxsw" YMM, 16, max(i16_1, i16_2));
            check("vpminsw" YMM, 16, min(i16_1, i16_2));
            check("vpmaxub" YMM, 32, max(u8_1, u8_2));
            check("vpminub" YMM, 32, min(u8_1, u8_2));
            check("vpmulhuw" YMM, 16, u16((u32(u16_1) * u32(u16_2))/(256*256)));
            check("vpmulhuw" YMM, 16, u16((u32(u16_1) * u32(u16_2))>>16));

            check("vpaddq" YMM, 8, i64_1 + i64_2);
            check("vpsubq" YMM, 8, i64_1 - i64_2);
            check(use_avx512_skylake ? "vpmullq" : "vpmuludq", 8, u64_1 * u64_2);

            check("vpackssdw", 16, i16_sat(i32_1));
            check("vpacksswb", 32, i8_sat(i16_1));
            check("vpackuswb", 32, u8_sat(i16_1));

            check("vpabsb", 32, abs(i8_1));
            check("vpabsw", 16, abs(i16_1));
            check("vpabsd", 8, abs(i32_1));

            // llvm doesn't distinguish between signed and unsigned multiplies
            // check("vpmuldq", 8, i64(i32_1) * i64(i32_2));
            if (!use_avx512) {
                // AVX512 uses widening loads instead
                check("vpmuludq" YMM, 8, u64(u32_1) * u64(u32_2));
            }
            check("vpmulld" YMM, 8, i32_1 * i32_2);

            check("vpblend*b" YMM, 32, select(u8_1 > 7, u8_1, u8_2));

            check("vpmaxsb" YMM, 32, max(i8_1, i8_2));
            check("vpminsb" YMM, 32, min(i8_1, i8_2));
            check("vpmaxuw" YMM, 16, max(u16_1, u16_2));
            check("vpminuw" YMM, 16, min(u16_1, u16_2));
            check("vpmaxud" YMM, 16, max(u32_1, u32_2));
            check("vpminud" YMM, 16, min(u32_1, u32_2));
            check("vpmaxsd" YMM, 8, max(i32_1, i32_2));
            check("vpminsd" YMM, 8, min(i32_1, i32_2));

            check("vpcmpeqq" YMM, 4, select(i64_1 == i64_2, i64(1), i64(2)));
            check("vpackusdw", 16, u16(clamp(i32_1, 0, max_u16)));
            check("vpcmpgtq" YMM, 4, select(i64_1 > i64_2, i64(1), i64(2)));
        }

        if (use_avx512) {
#if 0
            // Not yet implemented
            check("vrangeps", 16, clamp(f32_1, 3.0f, 9.0f));
            check("vrangepd", 8, clamp(f64_1, f64(3), f64(9)));

            check("vreduceps", 16, f32_1 - floor(f32_1));
            check("vreduceps", 16, f32_1 - floor(f32_1*8)/8);
            check("vreduceps", 16, f32_1 - trunc(f32_1));
            check("vreduceps", 16, f32_1 - trunc(f32_1*8)/8);
            check("vreducepd", 8, f64_1 - floor(f64_1));
            check("vreducepd", 8, f64_1 - floor(f64_1*8)/8);
            check("vreducepd", 8, f64_1 - trunc(f64_1));
            check("vreducepd", 8, f64_1 - trunc(f64_1*8)/8);
#endif
        }
        if (use_avx512_skylake) {
            check("vpabsq", 8, abs(i64_1));
            check("vpmaxuq", 8, max(u64_1, u64_2));
            check("vpminuq", 8, min(u64_1, u64_2));
            check("vpmaxsq", 8, max(i64_1, i64_2));
            check("vpminsq", 8, min(i64_1, i64_2));
        }
    }

    void check_neon_all() {
        Expr f64_1 = in_f64(x), f64_2 = in_f64(x+16), f64_3 = in_f64(x+32);
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x+16), f32_3 = in_f32(x+32);
        Expr i8_1  = in_i8(x),  i8_2  = in_i8(x+16),  i8_3  = in_i8(x+32);
        Expr u8_1  = in_u8(x),  u8_2  = in_u8(x+16),  u8_3  = in_u8(x+32);
        Expr i16_1 = in_i16(x), i16_2 = in_i16(x+16), i16_3 = in_i16(x+32);
        Expr u16_1 = in_u16(x), u16_2 = in_u16(x+16), u16_3 = in_u16(x+32);
        Expr i32_1 = in_i32(x), i32_2 = in_i32(x+16), i32_3 = in_i32(x+32);
        Expr u32_1 = in_u32(x), u32_2 = in_u32(x+16), u32_3 = in_u32(x+32);
        Expr i64_1 = in_i64(x), i64_2 = in_i64(x+16), i64_3 = in_i64(x+32);
        Expr u64_1 = in_u64(x), u64_2 = in_u64(x+16), u64_3 = in_u64(x+32);
        Expr bool_1 = (f32_1 > 0.3f), bool_2 = (f32_1 < -0.3f), bool_3 = (f32_1 != -0.34f);

        // Table copied from the Cortex-A9 TRM.

        // In general neon ops have the 64-bit version, the 128-bit
        // version (ending in q), and the widening version that takes
        // 64-bit args and produces a 128-bit result (ending in l). We try
        // to peephole match any with vector, so we just try 64-bits, 128
        // bits, 192 bits, and 256 bits for everything.

        bool arm32 = (target.bits == 32);

        for (int w = 1; w <= 4; w++) {

            // VABA     I       -       Absolute Difference and Accumulate
            check(arm32 ? "vaba.s8"  : "saba", 8*w, i8_1 + absd(i8_2, i8_3));
            check(arm32 ? "vaba.u8"  : "uaba", 8*w, u8_1 + absd(u8_2, u8_3));
            check(arm32 ? "vaba.s16" : "saba", 4*w, i16_1 + absd(i16_2, i16_3));
            check(arm32 ? "vaba.u16" : "uaba", 4*w, u16_1 + absd(u16_2, u16_3));
            check(arm32 ? "vaba.s32" : "saba", 2*w, i32_1 + absd(i32_2, i32_3));
            check(arm32 ? "vaba.u32" : "uaba", 2*w, u32_1 + absd(u32_2, u32_3));

            // VABAL    I       -       Absolute Difference and Accumulate Long
            check(arm32 ? "vabal.s8"  : "sabal", 8*w, i16_1 + absd(i8_2, i8_3));
            check(arm32 ? "vabal.u8"  : "uabal", 8*w, u16_1 + absd(u8_2, u8_3));
            check(arm32 ? "vabal.s16" : "sabal", 4*w, i32_1 + absd(i16_2, i16_3));
            check(arm32 ? "vabal.u16" : "uabal", 4*w, u32_1 + absd(u16_2, u16_3));
            check(arm32 ? "vabal.s32" : "sabal", 2*w, i64_1 + absd(i32_2, i32_3));
            check(arm32 ? "vabal.u32" : "uabal", 2*w, u64_1 + absd(u32_2, u32_3));

            // VABD     I, F    -       Absolute Difference
            check(arm32 ? "vabd.s8"  : "sabd", 8*w, absd(i8_2, i8_3));
            check(arm32 ? "vabd.u8"  : "uabd", 8*w, absd(u8_2, u8_3));
            check(arm32 ? "vabd.s16" : "sabd", 4*w, absd(i16_2, i16_3));
            check(arm32 ? "vabd.u16" : "uabd", 4*w, absd(u16_2, u16_3));
            check(arm32 ? "vabd.s32" : "sabd", 2*w, absd(i32_2, i32_3));
            check(arm32 ? "vabd.u32" : "uabd", 2*w, absd(u32_2, u32_3));

            // Via widening, taking abs, then narrowing
            check(arm32 ? "vabd.s8"  : "sabd", 8*w, u8(abs(i16(i8_2) - i8_3)));
            check(arm32 ? "vabd.u8"  : "uabd", 8*w, u8(abs(i16(u8_2) - u8_3)));
            check(arm32 ? "vabd.s16" : "sabd", 4*w, u16(abs(i32(i16_2) - i16_3)));
            check(arm32 ? "vabd.u16" : "uabd", 4*w, u16(abs(i32(u16_2) - u16_3)));
            check(arm32 ? "vabd.s32" : "sabd", 2*w, u32(abs(i64(i32_2) - i32_3)));
            check(arm32 ? "vabd.u32" : "uabd", 2*w, u32(abs(i64(u32_2) - u32_3)));

            // VABDL    I       -       Absolute Difference Long
            check(arm32 ? "vabdl.s8"  : "sabdl", 8*w, i16(absd(i8_2, i8_3)));
            check(arm32 ? "vabdl.u8"  : "uabdl", 8*w, u16(absd(u8_2, u8_3)));
            check(arm32 ? "vabdl.s16" : "sabdl", 4*w, i32(absd(i16_2, i16_3)));
            check(arm32 ? "vabdl.u16" : "uabdl", 4*w, u32(absd(u16_2, u16_3)));
            check(arm32 ? "vabdl.s32" : "sabdl", 2*w, i64(absd(i32_2, i32_3)));
            check(arm32 ? "vabdl.u32" : "uabdl", 2*w, u64(absd(u32_2, u32_3)));

            // Via widening then taking an abs
            check(arm32 ? "vabdl.s8"  : "sabdl", 8*w, abs(i16(i8_2) - i16(i8_3)));
            check(arm32 ? "vabdl.u8"  : "uabdl", 8*w, abs(i16(u8_2) - i16(u8_3)));
            check(arm32 ? "vabdl.s16" : "sabdl", 4*w, abs(i32(i16_2) - i32(i16_3)));
            check(arm32 ? "vabdl.u16" : "uabdl", 4*w, abs(i32(u16_2) - i32(u16_3)));
            check(arm32 ? "vabdl.s32" : "sabdl", 2*w, abs(i64(i32_2) - i64(i32_3)));
            check(arm32 ? "vabdl.u32" : "uabdl", 2*w, abs(i64(u32_2) - i64(u32_3)));

            // VABS     I, F    F, D    Absolute
            check(arm32 ? "vabs.f32" : "fabs", 2*w, abs(f32_1));
            check(arm32 ? "vabs.s32" : "abs", 2*w, abs(i32_1));
            check(arm32 ? "vabs.s16" : "abs", 4*w, abs(i16_1));
            check(arm32 ? "vabs.s8"  : "abs", 8*w, abs(i8_1));

            // VACGE    F       -       Absolute Compare Greater Than or Equal
            // VACGT    F       -       Absolute Compare Greater Than
            // VACLE    F       -       Absolute Compare Less Than or Equal
            // VACLT    F       -       Absolute Compare Less Than

            // VADD     I, F    F, D    Add
            check(arm32 ? "vadd.i8"  : "add", 8*w, i8_1 + i8_2);
            check(arm32 ? "vadd.i8"  : "add", 8*w, u8_1 + u8_2);
            check(arm32 ? "vadd.i16" : "add", 4*w, i16_1 + i16_2);
            check(arm32 ? "vadd.i16" : "add", 4*w, u16_1 + u16_2);
            check(arm32 ? "vadd.i32" : "add", 2*w, i32_1 + i32_2);
            check(arm32 ? "vadd.i32" : "add", 2*w, u32_1 + u32_2);
            check(arm32 ? "vadd.f32" : "fadd", 2*w, f32_1 + f32_2);
            check(arm32 ? "vadd.i64" : "add", 2*w, i64_1 + i64_2);
            check(arm32 ? "vadd.i64" : "add", 2*w, u64_1 + u64_2);

            // VADDHN   I       -       Add and Narrow Returning High Half
            check(arm32 ? "vaddhn.i16" : "addhn", 8*w, i8((i16_1 + i16_2)/256));
            check(arm32 ? "vaddhn.i16" : "addhn", 8*w, u8((u16_1 + u16_2)/256));
            check(arm32 ? "vaddhn.i32" : "addhn", 4*w, i16((i32_1 + i32_2)/65536));
            check(arm32 ? "vaddhn.i32" : "addhn", 4*w, u16((u32_1 + u32_2)/65536));

            // VADDL    I       -       Add Long
            check(arm32 ? "vaddl.s8"  : "saddl", 8*w, i16(i8_1) + i16(i8_2));
            check(arm32 ? "vaddl.u8"  : "uaddl", 8*w, u16(u8_1) + u16(u8_2));
            check(arm32 ? "vaddl.s16" : "saddl", 4*w, i32(i16_1) + i32(i16_2));
            check(arm32 ? "vaddl.u16" : "uaddl", 4*w, u32(u16_1) + u32(u16_2));
            check(arm32 ? "vaddl.s32" : "saddl", 2*w, i64(i32_1) + i64(i32_2));
            check(arm32 ? "vaddl.u32" : "uaddl", 2*w, u64(u32_1) + u64(u32_2));

            // VADDW    I       -       Add Wide
            check(arm32 ? "vaddw.s8"  : "saddw", 8*w, i8_1 + i16_1);
            check(arm32 ? "vaddw.u8"  : "uaddw", 8*w, u8_1 + u16_1);
            check(arm32 ? "vaddw.s16" : "saddw", 4*w, i16_1 + i32_1);
            check(arm32 ? "vaddw.u16" : "uaddw", 4*w, u16_1 + u32_1);
            check(arm32 ? "vaddw.s32" : "saddw", 2*w, i32_1 + i64_1);
            check(arm32 ? "vaddw.u32" : "uaddw", 2*w, u32_1 + u64_1);

            // VAND     X       -       Bitwise AND
            // Not implemented in front-end yet
            // check("vand", 4, bool1 & bool2);
            // check("vand", 2, bool1 & bool2);

            // VBIC     I       -       Bitwise Clear
            // VBIF     X       -       Bitwise Insert if False
            // VBIT     X       -       Bitwise Insert if True
            // skip these ones

            // VBSL     X       -       Bitwise Select
            check(arm32 ? "vbsl" : "bsl", 2*w, select(f32_1 > f32_2, 1.0f, 2.0f));

            // VCEQ     I, F    -       Compare Equal
            check(arm32 ? "vceq.i8"  : "cmeq", 8*w, select(i8_1 == i8_2, i8(1), i8(2)));
            check(arm32 ? "vceq.i8"  : "cmeq", 8*w, select(u8_1 == u8_2, u8(1), u8(2)));
            check(arm32 ? "vceq.i16" : "cmeq", 4*w, select(i16_1 == i16_2, i16(1), i16(2)));
            check(arm32 ? "vceq.i16" : "cmeq", 4*w, select(u16_1 == u16_2, u16(1), u16(2)));
            check(arm32 ? "vceq.i32" : "cmeq", 2*w, select(i32_1 == i32_2, i32(1), i32(2)));
            check(arm32 ? "vceq.i32" : "cmeq", 2*w, select(u32_1 == u32_2, u32(1), u32(2)));
            check(arm32 ? "vceq.f32" : "fcmeq", 2*w, select(f32_1 == f32_2, 1.0f, 2.0f));


            // VCGE     I, F    -       Compare Greater Than or Equal
#if 0
            // Halide flips these to less than instead
            check("vcge.s8", 16, select(i8_1 >= i8_2, i8(1), i8(2)));
            check("vcge.u8", 16, select(u8_1 >= u8_2, u8(1), u8(2)));
            check("vcge.s16", 8, select(i16_1 >= i16_2, i16(1), i16(2)));
            check("vcge.u16", 8, select(u16_1 >= u16_2, u16(1), u16(2)));
            check("vcge.s32", 4, select(i32_1 >= i32_2, i32(1), i32(2)));
            check("vcge.u32", 4, select(u32_1 >= u32_2, u32(1), u32(2)));
            check("vcge.f32", 4, select(f32_1 >= f32_2, 1.0f, 2.0f));
            check("vcge.s8", 8, select(i8_1 >= i8_2, i8(1), i8(2)));
            check("vcge.u8", 8, select(u8_1 >= u8_2, u8(1), u8(2)));
            check("vcge.s16", 4, select(i16_1 >= i16_2, i16(1), i16(2)));
            check("vcge.u16", 4, select(u16_1 >= u16_2, u16(1), u16(2)));
            check("vcge.s32", 2, select(i32_1 >= i32_2, i32(1), i32(2)));
            check("vcge.u32", 2, select(u32_1 >= u32_2, u32(1), u32(2)));
            check("vcge.f32", 2, select(f32_1 >= f32_2, 1.0f, 2.0f));
#endif

            // VCGT     I, F    -       Compare Greater Than
            check(arm32 ? "vcgt.s8"  : "cmgt", 8*w, select(i8_1 > i8_2, i8(1), i8(2)));
            check(arm32 ? "vcgt.u8"  : "cmhi", 8*w, select(u8_1 > u8_2, u8(1), u8(2)));
            check(arm32 ? "vcgt.s16" : "cmgt", 4*w, select(i16_1 > i16_2, i16(1), i16(2)));
            check(arm32 ? "vcgt.u16" : "cmhi", 4*w, select(u16_1 > u16_2, u16(1), u16(2)));
            check(arm32 ? "vcgt.s32" : "cmgt", 2*w, select(i32_1 > i32_2, i32(1), i32(2)));
            check(arm32 ? "vcgt.u32" : "cmhi", 2*w, select(u32_1 > u32_2, u32(1), u32(2)));
            check(arm32 ? "vcgt.f32" : "fcmgt", 2*w, select(f32_1 > f32_2, 1.0f, 2.0f));

            // VCLS     I       -       Count Leading Sign Bits
            // VCLZ     I       -       Count Leading Zeros
            // VCMP     -       F, D    Compare Setting Flags
            // VCNT     I       -       Count Number of Set Bits
            // We skip these ones

            // VCVT     I, F, H I, F, D, H      Convert Between Floating-Point and 32-bit Integer Types
            check(arm32 ? "vcvt.f32.u32" : "ucvtf", 2*w, f32(u32_1));
            check(arm32 ? "vcvt.f32.s32" : "scvtf", 2*w, f32(i32_1));
            check(arm32 ? "vcvt.u32.f32" : "fcvtzu", 2*w, u32(f32_1));
            check(arm32 ? "vcvt.s32.f32" : "fcvtzs", 2*w, i32(f32_1));
            // skip the fixed point conversions for now

            // VDIV     -       F, D    Divide
            // This doesn't actually get vectorized in 32-bit. Not sure cortex processors can do vectorized division.
            check(arm32 ? "vdiv.f32" : "fdiv", 2*w, f32_1/f32_2);
            check(arm32 ? "vdiv.f64" : "fdiv", 2*w, f64_1/f64_2);

            // VDUP     X       -       Duplicate
            check(arm32 ? "vdup.8"  : "dup", 16*w, i8(y));
            check(arm32 ? "vdup.8"  : "dup", 16*w, u8(y));
            check(arm32 ? "vdup.16" : "dup", 8*w, i16(y));
            check(arm32 ? "vdup.16" : "dup", 8*w, u16(y));
            check(arm32 ? "vdup.32" : "dup", 4*w, i32(y));
            check(arm32 ? "vdup.32" : "dup", 4*w, u32(y));
            check(arm32 ? "vdup.32" : "dup", 4*w, f32(y));

            // VEOR     X       -       Bitwise Exclusive OR
            // check("veor", 4, bool1 ^ bool2);

            // VEXT     I       -       Extract Elements and Concatenate
            // unaligned loads with known offsets should use vext
#if 0
            // We currently don't do this.
            check("vext.8", 16, in_i8(x+1));
            check("vext.16", 8, in_i16(x+1));
            check("vext.32", 4, in_i32(x+1));
#endif

            // VHADD    I       -       Halving Add
            check(arm32 ? "vhadd.s8"  : "shadd", 8*w, i8((i16(i8_1) + i16(i8_2))/2));
            check(arm32 ? "vhadd.u8"  : "uhadd", 8*w, u8((u16(u8_1) + u16(u8_2))/2));
            check(arm32 ? "vhadd.s16" : "shadd", 4*w, i16((i32(i16_1) + i32(i16_2))/2));
            check(arm32 ? "vhadd.u16" : "uhadd", 4*w, u16((u32(u16_1) + u32(u16_2))/2));
            check(arm32 ? "vhadd.s32" : "shadd", 2*w, i32((i64(i32_1) + i64(i32_2))/2));
            check(arm32 ? "vhadd.u32" : "uhadd", 2*w, u32((u64(u32_1) + u64(u32_2))/2));

            // Halide doesn't define overflow behavior for i32 so we
            // can use vhadd instruction. We can't use it for unsigned u8,i16,u16,u32.
            check(arm32 ? "vhadd.s32" : "shadd", 2*w, (i32_1 + i32_2)/2);

            // VHSUB    I       -       Halving Subtract
            check(arm32 ? "vhsub.s8"  : "shsub", 8*w, i8((i16(i8_1) - i16(i8_2))/2));
            check(arm32 ? "vhsub.u8"  : "uhsub", 8*w, u8((u16(u8_1) - u16(u8_2))/2));
            check(arm32 ? "vhsub.s16" : "shsub", 4*w, i16((i32(i16_1) - i32(i16_2))/2));
            check(arm32 ? "vhsub.u16" : "uhsub", 4*w, u16((u32(u16_1) - u32(u16_2))/2));
            check(arm32 ? "vhsub.s32" : "shsub", 2*w, i32((i64(i32_1) - i64(i32_2))/2));
            check(arm32 ? "vhsub.u32" : "uhsub", 2*w, u32((u64(u32_1) - u64(u32_2))/2));

            check(arm32 ? "vhsub.s32" : "shsub", 2*w, (i32_1 - i32_2)/2);

            // VLD1     X       -       Load Single-Element Structures
            // dense loads with unknown alignments should use vld1 variants
            check(arm32 ? "vld1.8"  : "ldr", 8*w, in_i8(x+y));
            check(arm32 ? "vld1.8"  : "ldr", 8*w, in_u8(x+y));
            check(arm32 ? "vld1.16" : "ldr", 4*w, in_i16(x+y));
            check(arm32 ? "vld1.16" : "ldr", 4*w, in_u16(x+y));
            if (w > 1) {
                // When w == 1, llvm emits vldr instead
                check(arm32 ? "vld1.32" : "ldr", 2*w, in_i32(x+y));
                check(arm32 ? "vld1.32" : "ldr", 2*w, in_u32(x+y));
                check(arm32 ? "vld1.32" : "ldr", 2*w, in_f32(x+y));
            }

            // VLD2     X       -       Load Two-Element Structures
            check(arm32 ? "vld2.32" : "ld2", 4*w, in_i32(x*2) + in_i32(x*2+1));
            check(arm32 ? "vld2.32" : "ld2", 4*w, in_u32(x*2) + in_u32(x*2+1));
            check(arm32 ? "vld2.32" : "ld2", 4*w, in_f32(x*2) + in_f32(x*2+1));
            check(arm32 ? "vld2.8"  : "ld2", 8*w, in_i8(x*2) + in_i8(x*2+1));
            check(arm32 ? "vld2.8"  : "ld2", 8*w, in_u8(x*2) + in_u8(x*2+1));
            check(arm32 ? "vld2.16" : "ld2", 4*w, in_i16(x*2) + in_i16(x*2+1));
            check(arm32 ? "vld2.16" : "ld2", 4*w, in_u16(x*2) + in_u16(x*2+1));


            // VLD3     X       -       Load Three-Element Structures
            check(arm32 ? "vld3.32" : "ld3", 4*w, in_i32(x*3+y));
            check(arm32 ? "vld3.32" : "ld3", 4*w, in_u32(x*3+y));
            check(arm32 ? "vld3.32" : "ld3", 4*w, in_f32(x*3+y));
            check(arm32 ? "vld3.8"  : "ld3", 8*w, in_i8(x*3+y));
            check(arm32 ? "vld3.8"  : "ld3", 8*w, in_u8(x*3+y));
            check(arm32 ? "vld3.16" : "ld3", 4*w, in_i16(x*3+y));
            check(arm32 ? "vld3.16" : "ld3", 4*w, in_u16(x*3+y));

            // VLD4     X       -       Load Four-Element Structures
            check(arm32 ? "vld4.32" : "ld4", 4*w, in_i32(x*4+y));
            check(arm32 ? "vld4.32" : "ld4", 4*w, in_u32(x*4+y));
            check(arm32 ? "vld4.32" : "ld4", 4*w, in_f32(x*4+y));
            check(arm32 ? "vld4.8"  : "ld4", 8*w, in_i8(x*4+y));
            check(arm32 ? "vld4.8"  : "ld4", 8*w, in_u8(x*4+y));
            check(arm32 ? "vld4.16" : "ld4", 4*w, in_i16(x*4+y));
            check(arm32 ? "vld4.16" : "ld4", 4*w, in_u16(x*4+y));

            // VLDM     X       F, D    Load Multiple Registers
            // VLDR     X       F, D    Load Single Register
            // We generally generate vld instead

            // VMAX     I, F    -       Maximum
            check(arm32 ? "vmax.s8" : "smax", 8*w, max(i8_1, i8_2));
            check(arm32 ? "vmax.u8" : "umax", 8*w, max(u8_1, u8_2));
            check(arm32 ? "vmax.s16" : "smax", 4*w, max(i16_1, i16_2));
            check(arm32 ? "vmax.u16" : "umax", 4*w, max(u16_1, u16_2));
            check(arm32 ? "vmax.s32" : "smax", 2*w, max(i32_1, i32_2));
            check(arm32 ? "vmax.u32" : "umax", 2*w, max(u32_1, u32_2));
            check(arm32 ? "vmax.f32" : "fmax", 2*w, max(f32_1, f32_2));

            // VMIN     I, F    -       Minimum
            check(arm32 ? "vmin.s8" : "smin", 8*w, min(i8_1, i8_2));
            check(arm32 ? "vmin.u8" : "umin", 8*w, min(u8_1, u8_2));
            check(arm32 ? "vmin.s16" : "smin", 4*w, min(i16_1, i16_2));
            check(arm32 ? "vmin.u16" : "umin", 4*w, min(u16_1, u16_2));
            check(arm32 ? "vmin.s32" : "smin", 2*w, min(i32_1, i32_2));
            check(arm32 ? "vmin.u32" : "umin", 2*w, min(u32_1, u32_2));
            check(arm32 ? "vmin.f32" : "fmin", 2*w, min(f32_1, f32_2));

            // VMLA     I, F    F, D    Multiply Accumulate
            check(arm32 ? "vmla.i8"  : "mla", 8*w, i8_1 + i8_2*i8_3);
            check(arm32 ? "vmla.i8"  : "mla", 8*w, u8_1 + u8_2*u8_3);
            check(arm32 ? "vmla.i16" : "mla", 4*w, i16_1 + i16_2*i16_3);
            check(arm32 ? "vmla.i16" : "mla", 4*w, u16_1 + u16_2*u16_3);
            check(arm32 ? "vmla.i32" : "mla", 2*w, i32_1 + i32_2*i32_3);
            check(arm32 ? "vmla.i32" : "mla", 2*w, u32_1 + u32_2*u32_3);
            if (w == 1 || w == 2) {
                // Older llvms don't always fuse this at non-native widths
                check(arm32 ? "vmla.f32" : "fmla", 2*w, f32_1 + f32_2*f32_3);
            }

            // VMLS     I, F    F, D    Multiply Subtract
            check(arm32 ? "vmls.i8"  : "mls", 8*w, i8_1 - i8_2*i8_3);
            check(arm32 ? "vmls.i8"  : "mls", 8*w, u8_1 - u8_2*u8_3);
            check(arm32 ? "vmls.i16" : "mls", 4*w, i16_1 - i16_2*i16_3);
            check(arm32 ? "vmls.i16" : "mls", 4*w, u16_1 - u16_2*u16_3);
            check(arm32 ? "vmls.i32" : "mls", 2*w, i32_1 - i32_2*i32_3);
            check(arm32 ? "vmls.i32" : "mls", 2*w, u32_1 - u32_2*u32_3);
            if (w == 1 || w == 2) {
                // Older llvms don't always fuse this at non-native widths
                check(arm32 ? "vmls.f32" : "fmls", 2*w, f32_1 - f32_2*f32_3);
            }

            // VMLAL    I       -       Multiply Accumulate Long
            check(arm32 ? "vmlal.s8"  : "smlal", 8*w, i16_1 + i16(i8_2)*i8_3);
            check(arm32 ? "vmlal.u8"  : "umlal", 8*w, u16_1 + u16(u8_2)*u8_3);
            check(arm32 ? "vmlal.s16" : "smlal", 4*w, i32_1 + i32(i16_2)*i16_3);
            check(arm32 ? "vmlal.u16" : "umlal", 4*w, u32_1 + u32(u16_2)*u16_3);
            check(arm32 ? "vmlal.s32" : "smlal", 2*w, i64_1 + i64(i32_2)*i32_3);
            check(arm32 ? "vmlal.u32" : "umlal", 2*w, u64_1 + u64(u32_2)*u32_3);

            // VMLSL    I       -       Multiply Subtract Long
            check(arm32 ? "vmlsl.s8"  : "smlsl", 8*w, i16_1 - i16(i8_2)*i8_3);
            check(arm32 ? "vmlsl.u8"  : "umlsl", 8*w, u16_1 - u16(u8_2)*u8_3);
            check(arm32 ? "vmlsl.s16" : "smlsl", 4*w, i32_1 - i32(i16_2)*i16_3);
            check(arm32 ? "vmlsl.u16" : "umlsl", 4*w, u32_1 - u32(u16_2)*u16_3);
            check(arm32 ? "vmlsl.s32" : "smlsl", 2*w, i64_1 - i64(i32_2)*i32_3);
            check(arm32 ? "vmlsl.u32" : "umlsl", 2*w, u64_1 - u64(u32_2)*u32_3);

            // VMOV     X       F, D    Move Register or Immediate
            // This is for loading immediates, which we won't do in the inner loop anyway

            // VMOVL    I       -       Move Long
            // For aarch64, llvm does a widening shift by 0 instead of using the sxtl instruction.
            check(arm32 ? "vmovl.s8"  : "sshll", 8*w, i16(i8_1));
            check(arm32 ? "vmovl.u8"  : "ushll", 8*w, u16(u8_1));
            check(arm32 ? "vmovl.u8"  : "ushll", 8*w, i16(u8_1));
            check(arm32 ? "vmovl.s16" : "sshll", 4*w, i32(i16_1));
            check(arm32 ? "vmovl.u16" : "ushll", 4*w, u32(u16_1));
            check(arm32 ? "vmovl.u16" : "ushll", 4*w, i32(u16_1));
            check(arm32 ? "vmovl.s32" : "sshll", 2*w, i64(i32_1));
            check(arm32 ? "vmovl.u32" : "ushll", 2*w, u64(u32_1));
            check(arm32 ? "vmovl.u32" : "ushll", 2*w, i64(u32_1));

            // VMOVN    I       -       Move and Narrow
            check(arm32 ? "vmovn.i16" : "xtn", 8*w, i8(i16_1));
            check(arm32 ? "vmovn.i16" : "xtn", 8*w, u8(u16_1));
            check(arm32 ? "vmovn.i32" : "xtn", 4*w, i16(i32_1));
            check(arm32 ? "vmovn.i32" : "xtn", 4*w, u16(u32_1));
            check(arm32 ? "vmovn.i64" : "xtn", 2*w, i32(i64_1));
            check(arm32 ? "vmovn.i64" : "xtn", 2*w, u32(u64_1));

            // VMRS     X       F, D    Move Advanced SIMD or VFP Register to ARM compute Engine
            // VMSR     X       F, D    Move ARM Core Register to Advanced SIMD or VFP
            // trust llvm to use this correctly

            // VMUL     I, F, P F, D    Multiply
            check(arm32 ? "vmul.f64" : "fmul", 2*w, f64_2*f64_1);
            check(arm32 ? "vmul.i8"  : "mul",  8*w, i8_2*i8_1);
            check(arm32 ? "vmul.i8"  : "mul",  8*w, u8_2*u8_1);
            check(arm32 ? "vmul.i16" : "mul",  4*w, i16_2*i16_1);
            check(arm32 ? "vmul.i16" : "mul",  4*w, u16_2*u16_1);
            check(arm32 ? "vmul.i32" : "mul",  2*w, i32_2*i32_1);
            check(arm32 ? "vmul.i32" : "mul",  2*w, u32_2*u32_1);
            check(arm32 ? "vmul.f32" : "fmul", 2*w, f32_2*f32_1);

            // VMULL    I, F, P -       Multiply Long
            check(arm32 ? "vmull.s8"  : "smull", 8*w, i16(i8_1)*i8_2);
            check(arm32 ? "vmull.u8"  : "umull", 8*w, u16(u8_1)*u8_2);
            check(arm32 ? "vmull.s16" : "smull", 4*w, i32(i16_1)*i16_2);
            check(arm32 ? "vmull.u16" : "umull", 4*w, u32(u16_1)*u16_2);
            check(arm32 ? "vmull.s32" : "smull", 2*w, i64(i32_1)*i32_2);
            check(arm32 ? "vmull.u32" : "umull", 2*w, u64(u32_1)*u32_2);

            // integer division by a constant should use fixed point unsigned
            // multiplication, which is done by using a widening multiply
            // followed by a narrowing
            check(arm32 ? "vmull.u8"  : "umull", 8*w, i8_1/37);
            check(arm32 ? "vmull.u8"  : "umull", 8*w, u8_1/37);
            check(arm32 ? "vmull.u16" : "umull", 4*w, i16_1/37);
            check(arm32 ? "vmull.u16" : "umull", 4*w, u16_1/37);
            check(arm32 ? "vmull.u32" : "umull", 2*w, i32_1/37);
            check(arm32 ? "vmull.u32" : "umull", 2*w, u32_1/37);

            // VMVN     X       -       Bitwise NOT
            // check("vmvn", ~bool1);

            // VNEG     I, F    F, D    Negate
            check(arm32 ? "vneg.s8"  : "neg", 8*w, -i8_1);
            check(arm32 ? "vneg.s16" : "neg", 4*w, -i16_1);
            check(arm32 ? "vneg.s32" : "neg", 2*w, -i32_1);
            check(arm32 ? "vneg.f32" : "fneg", 4*w, -f32_1);
            check(arm32 ? "vneg.f64" : "fneg", 2*w, -f64_1);

            // VNMLA    -       F, D    Negative Multiply Accumulate
            // VNMLS    -       F, D    Negative Multiply Subtract
            // VNMUL    -       F, D    Negative Multiply
#if 0
            // These are vfp, not neon. They only work on scalars
            check("vnmla.f32", 4, -(f32_1 + f32_2*f32_3));
            check("vnmla.f64", 2, -(f64_1 + f64_2*f64_3));
            check("vnmls.f32", 4, -(f32_1 - f32_2*f32_3));
            check("vnmls.f64", 2, -(f64_1 - f64_2*f64_3));
            check("vnmul.f32", 4, -(f32_1*f32_2));
            check("vnmul.f64", 2, -(f64_1*f64_2));
#endif

            // VORN     X       -       Bitwise OR NOT
            // check("vorn", bool1 | (~bool2));

            // VORR     X       -       Bitwise OR
            // check("vorr", bool1 | bool2);

            // VPADAL   I       -       Pairwise Add and Accumulate Long
            // VPADD    I, F    -       Pairwise Add
            // VPADDL   I       -       Pairwise Add Long
            // VPMAX    I, F    -       Pairwise Maximum
            // VPMIN    I, F    -       Pairwise Minimum
            // We don't do horizontal ops

            // VPOP     X       F, D    Pop from Stack
            // VPUSH    X       F, D    Push to Stack
            // Not used by us

            // VQABS    I       -       Saturating Absolute
#if 0
            // Of questionable value. Catching abs calls is annoying, and the
            // slow path is only one more op (for the max).
            check("vqabs.s8", 16, abs(max(i8_1, -max_i8)));
            check("vqabs.s8", 8, abs(max(i8_1, -max_i8)));
            check("vqabs.s16", 8, abs(max(i16_1, -max_i16)));
            check("vqabs.s16", 4, abs(max(i16_1, -max_i16)));
            check("vqabs.s32", 4, abs(max(i32_1, -max_i32)));
            check("vqabs.s32", 2, abs(max(i32_1, -max_i32)));
#endif

            // VQADD    I       -       Saturating Add
            check(arm32 ? "vqadd.s8"  : "sqadd", 8*w,  i8_sat(i16(i8_1)  + i16(i8_2)));
            check(arm32 ? "vqadd.s16" : "sqadd", 4*w, i16_sat(i32(i16_1) + i32(i16_2)));
            check(arm32 ? "vqadd.s32" : "sqadd", 2*w, i32_sat(i64(i32_1) + i64(i32_2)));

            check(arm32 ? "vqadd.u8"  : "uqadd", 8*w,  u8(min(u16(u8_1)  + u16(u8_2),  max_u8)));
            check(arm32 ? "vqadd.u16" : "uqadd", 4*w, u16(min(u32(u16_1) + u32(u16_2), max_u16)));

            // Check the case where we add a constant that could be narrowed
            check(arm32 ? "vqadd.u8"  : "uqadd", 8*w,  u8(min(u16(u8_1)  + 17,  max_u8)));
            check(arm32 ? "vqadd.u16" : "uqadd", 4*w, u16(min(u32(u16_1) + 17, max_u16)));

            // Can't do larger ones because we only have i32 constants

            // VQDMLAL  I       -       Saturating Double Multiply Accumulate Long
            // VQDMLSL  I       -       Saturating Double Multiply Subtract Long
            // VQDMULH  I       -       Saturating Doubling Multiply Returning High Half
            // VQDMULL  I       -       Saturating Doubling Multiply Long
            // Not sure why I'd use these

            // VQMOVN   I       -       Saturating Move and Narrow
            check(arm32 ? "vqmovn.s16" : "sqxtn", 8*w,  i8_sat(i16_1));
            check(arm32 ? "vqmovn.s32" : "sqxtn", 4*w, i16_sat(i32_1));
            check(arm32 ? "vqmovn.s64" : "sqxtn", 2*w, i32_sat(i64_1));
            check(arm32 ? "vqmovn.u16" : "uqxtn", 8*w,  u8(min(u16_1, max_u8)));
            check(arm32 ? "vqmovn.u32" : "uqxtn", 4*w, u16(min(u32_1, max_u16)));
            check(arm32 ? "vqmovn.u64" : "uqxtn", 2*w, u32(min(u64_1, max_u32)));

            // VQMOVUN  I       -       Saturating Move and Unsigned Narrow
            check(arm32 ? "vqmovun.s16" : "sqxtun", 8*w, u8_sat(i16_1));
            check(arm32 ? "vqmovun.s32" : "sqxtun", 4*w, u16_sat(i32_1));
            check(arm32 ? "vqmovun.s64" : "sqxtun", 2*w, u32_sat(i64_1));

            // VQNEG    I       -       Saturating Negate
            check(arm32 ? "vqneg.s8" : "sqneg",  8*w, -max(i8_1,  -max_i8));
            check(arm32 ? "vqneg.s16" : "sqneg", 4*w, -max(i16_1, -max_i16));
            check(arm32 ? "vqneg.s32" : "sqneg", 2*w, -max(i32_1, -max_i32));

            // VQRDMULH I       -       Saturating Rounding Doubling Multiply Returning High Half
            // Note: division in Halide always rounds down (not towards
            // zero). Otherwise these patterns would be more complicated.
            check(arm32 ? "vqrdmulh.s16" : "sqrdmulh", 4*w, i16_sat((i32(i16_1) * i32(i16_2) + (1<<14)) / (1 << 15)));
            check(arm32 ? "vqrdmulh.s32" : "sqrdmulh", 2*w, i32_sat((i64(i32_1) * i64(i32_2) + (1<<30)) /
                                                                    (Expr(int64_t(1)) << 31)));

            // VQRSHL   I       -       Saturating Rounding Shift Left
            // VQRSHRN  I       -       Saturating Rounding Shift Right Narrow
            // VQRSHRUN I       -       Saturating Rounding Shift Right Unsigned Narrow
            // We use the non-rounding form of these (at worst we do an extra add)

            // VQSHL    I       -       Saturating Shift Left
            check(arm32 ? "vqshl.s8"  : "sqshl", 8*w,  i8_sat(i16(i8_1)*16));
            check(arm32 ? "vqshl.s16" : "sqshl", 4*w, i16_sat(i32(i16_1)*16));
            check(arm32 ? "vqshl.s32" : "sqshl", 2*w, i32_sat(i64(i32_1)*16));
            check(arm32 ? "vqshl.u8"  : "uqshl",  8*w,  u8(min(u16(u8_1 )*16, max_u8)));
            check(arm32 ? "vqshl.u16" : "uqshl", 4*w, u16(min(u32(u16_1)*16, max_u16)));
            check(arm32 ? "vqshl.u32" : "uqshl", 2*w, u32(min(u64(u32_1)*16, max_u32)));

            // VQSHLU   I       -       Saturating Shift Left Unsigned
            check(arm32 ? "vqshlu.s8"  : "sqshlu", 8*w,  u8_sat(i16(i8_1)*16));
            check(arm32 ? "vqshlu.s16" : "sqshlu", 4*w, u16_sat(i32(i16_1)*16));
            check(arm32 ? "vqshlu.s32" : "sqshlu", 2*w, u32_sat(i64(i32_1)*16));


            // VQSHRN   I       -       Saturating Shift Right Narrow
            // VQSHRUN  I       -       Saturating Shift Right Unsigned Narrow
            check(arm32 ? "vqshrn.s16"  : "sqshrn",  8*w,  i8_sat(i16_1/16));
            check(arm32 ? "vqshrn.s32"  : "sqshrn",  4*w, i16_sat(i32_1/16));
            check(arm32 ? "vqshrn.s64"  : "sqshrn",  2*w, i32_sat(i64_1/16));
            check(arm32 ? "vqshrun.s16" : "sqshrun", 8*w,  u8_sat(i16_1/16));
            check(arm32 ? "vqshrun.s32" : "sqshrun", 4*w, u16_sat(i32_1/16));
            check(arm32 ? "vqshrun.s64" : "sqshrun", 2*w, u32_sat(i64_1/16));
            check(arm32 ? "vqshrn.u16"  : "uqshrn", 8*w,  u8(min(u16_1/16, max_u8)));
            check(arm32 ? "vqshrn.u32"  : "uqshrn", 4*w, u16(min(u32_1/16, max_u16)));
            check(arm32 ? "vqshrn.u64"  : "uqshrn", 2*w, u32(min(u64_1/16, max_u32)));

            // VQSUB    I       -       Saturating Subtract
            check(arm32 ? "vqsub.s8"  : "sqsub", 8*w,  i8_sat(i16(i8_1)  - i16(i8_2)));
            check(arm32 ? "vqsub.s16" : "sqsub", 4*w, i16_sat(i32(i16_1) - i32(i16_2)));
            check(arm32 ? "vqsub.s32" : "sqsub", 2*w, i32_sat(i64(i32_1) - i64(i32_2)));

            // N.B. Saturating subtracts are expressed by widening to a *signed* type
            check(arm32 ? "vqsub.u8"  : "uqsub",  8*w,  u8_sat(i16(u8_1)  - i16(u8_2)));
            check(arm32 ? "vqsub.u16" : "uqsub", 4*w, u16_sat(i32(u16_1) - i32(u16_2)));
            check(arm32 ? "vqsub.u32" : "uqsub", 2*w, u32_sat(i64(u32_1) - i64(u32_2)));

            // VRADDHN  I       -       Rounding Add and Narrow Returning High Half
#if 0
            // No rounding ops
            check("vraddhn.i16", 8, i8((i16_1 + i16_2 + 128)/256));
            check("vraddhn.i16", 8, u8((u16_1 + u16_2 + 128)/256));
            check("vraddhn.i32", 4, i16((i32_1 + i32_2 + 32768)/65536));
            check("vraddhn.i32", 4, u16((u32_1 + u32_2 + 32768)/65536));
#endif

            // VRECPE   I, F    -       Reciprocal Estimate
            check(arm32 ? "vrecpe.f32" : "frecpe", 2*w, fast_inverse(f32_1));

            // VRECPS   F       -       Reciprocal Step
            check(arm32 ? "vrecps.f32" : "frecps", 2*w, fast_inverse(f32_1));

            // VREV16   X       -       Reverse in Halfwords
            // VREV32   X       -       Reverse in Words
            // VREV64   X       -       Reverse in Doublewords

            // These reverse within each halfword, word, and doubleword
            // respectively. Sometimes llvm generates them, and sometimes
            // it generates vtbl instructions.

            // VRHADD   I       -       Rounding Halving Add
            check(arm32 ? "vrhadd.s8"  : "srhadd", 8*w,  i8((i16(i8_1 ) + i16(i8_2 ) + 1)/2));
            check(arm32 ? "vrhadd.u8"  : "urhadd", 8*w,  u8((u16(u8_1 ) + u16(u8_2 ) + 1)/2));
            check(arm32 ? "vrhadd.s16" : "srhadd", 4*w, i16((i32(i16_1) + i32(i16_2) + 1)/2));
            check(arm32 ? "vrhadd.u16" : "urhadd", 4*w, u16((u32(u16_1) + u32(u16_2) + 1)/2));
            check(arm32 ? "vrhadd.s32" : "srhadd", 2*w, i32((i64(i32_1) + i64(i32_2) + 1)/2));
            check(arm32 ? "vrhadd.u32" : "urhadd", 2*w, u32((u64(u32_1) + u64(u32_2) + 1)/2));

            // VRSHL    I       -       Rounding Shift Left
            // VRSHR    I       -       Rounding Shift Right
            // VRSHRN   I       -       Rounding Shift Right Narrow
            // We use the non-rounding forms of these

            // VRSQRTE  I, F    -       Reciprocal Square Root Estimate
            check(arm32 ? "vrsqrte.f32" : "frsqrte", 4*w, fast_inverse_sqrt(f32_1));

            // VRSQRTS  F       -       Reciprocal Square Root Step
            check(arm32 ? "vrsqrts.f32" : "frsqrts", 4*w, fast_inverse_sqrt(f32_1));

            // VRSRA    I       -       Rounding Shift Right and Accumulate
            // VRSUBHN  I       -       Rounding Subtract and Narrow Returning High Half
            // Boo rounding ops

            // VSHL     I       -       Shift Left
            check(arm32 ? "vshl.i64" : "shl", 2*w, i64_1*16);
            check(arm32 ? "vshl.i8"  : "shl", 8*w,  i8_1*16);
            check(arm32 ? "vshl.i16" : "shl", 4*w, i16_1*16);
            check(arm32 ? "vshl.i32" : "shl", 2*w, i32_1*16);
            check(arm32 ? "vshl.i64" : "shl", 2*w, u64_1*16);
            check(arm32 ? "vshl.i8"  : "shl", 8*w,  u8_1*16);
            check(arm32 ? "vshl.i16" : "shl", 4*w, u16_1*16);
            check(arm32 ? "vshl.i32" : "shl", 2*w, u32_1*16);


            // VSHLL    I       -       Shift Left Long
            check(arm32 ? "vshll.s8"  : "sshll", 8*w, i16(i8_1)*16);
            check(arm32 ? "vshll.s16" : "sshll", 4*w, i32(i16_1)*16);
            check(arm32 ? "vshll.s32" : "sshll", 2*w, i64(i32_1)*16);
            check(arm32 ? "vshll.u8"  : "ushll", 8*w, u16(u8_1)*16);
            check(arm32 ? "vshll.u16" : "ushll", 4*w, u32(u16_1)*16);
            check(arm32 ? "vshll.u32" : "ushll", 2*w, u64(u32_1)*16);

            // VSHR     I	-	Shift Right
            check(arm32 ? "vshr.s64" : "sshr", 2*w, i64_1/16);
            check(arm32 ? "vshr.s8"  : "sshr", 8*w,  i8_1/16);
            check(arm32 ? "vshr.s16" : "sshr", 4*w, i16_1/16);
            check(arm32 ? "vshr.s32" : "sshr", 2*w, i32_1/16);
            check(arm32 ? "vshr.u64" : "ushr", 2*w, u64_1/16);
            check(arm32 ? "vshr.u8"  : "ushr", 8*w,  u8_1/16);
            check(arm32 ? "vshr.u16" : "ushr", 4*w, u16_1/16);
            check(arm32 ? "vshr.u32" : "ushr", 2*w, u32_1/16);

            // VSHRN	I	-	Shift Right Narrow
            check(arm32 ? "vshrn.i16" : "shrn", 8*w,  i8(i16_1/256));
            check(arm32 ? "vshrn.i32" : "shrn", 4*w, i16(i32_1/65536));
            check(arm32 ? "vshrn.i16" : "shrn", 8*w,  u8(u16_1/256));
            check(arm32 ? "vshrn.i32" : "shrn", 4*w, u16(u32_1/65536));
            check(arm32 ? "vshrn.i16" : "shrn", 8*w,  i8(i16_1/16));
            check(arm32 ? "vshrn.i32" : "shrn", 4*w, i16(i32_1/16));
            check(arm32 ? "vshrn.i16" : "shrn", 8*w,  u8(u16_1/16));
            check(arm32 ? "vshrn.i32" : "shrn", 4*w, u16(u32_1/16));

            // VSLI	X	-	Shift Left and Insert
            // I guess this could be used for (x*256) | (y & 255)? We don't do bitwise ops on integers, so skip it.

            // VSQRT	-	F, D	Square Root
            check(arm32 ? "vsqrt.f32" : "fsqrt", 4*w, sqrt(f32_1));
            check(arm32 ? "vsqrt.f64" : "fsqrt", 2*w, sqrt(f64_1));

            // VSRA	I	-	Shift Right and Accumulate
            check(arm32 ? "vsra.s64" : "ssra", 2*w, i64_2 + i64_1/16);
            check(arm32 ? "vsra.s8"  : "ssra", 8*w,  i8_2 + i8_1/16);
            check(arm32 ? "vsra.s16" : "ssra", 4*w, i16_2 + i16_1/16);
            check(arm32 ? "vsra.s32" : "ssra", 2*w, i32_2 + i32_1/16);
            check(arm32 ? "vsra.u64" : "usra", 2*w, u64_2 + u64_1/16);
            check(arm32 ? "vsra.u8"  : "usra", 8*w,  u8_2 + u8_1/16);
            check(arm32 ? "vsra.u16" : "usra", 4*w, u16_2 + u16_1/16);
            check(arm32 ? "vsra.u32" : "usra", 2*w, u32_2 + u32_1/16);

            // VSRI	X	-	Shift Right and Insert
            // See VSLI


            // VSUB	I, F	F, D	Subtract
            check(arm32 ? "vsub.i64" : "sub",  2*w, i64_1 - i64_2);
            check(arm32 ? "vsub.i64" : "sub",  2*w, u64_1 - u64_2);
            check(arm32 ? "vsub.f32" : "fsub", 4*w, f32_1 - f32_2);
            check(arm32 ? "vsub.i8"  : "sub",  8*w,  i8_1 - i8_2);
            check(arm32 ? "vsub.i8"  : "sub",  8*w,  u8_1 - u8_2);
            check(arm32 ? "vsub.i16" : "sub",  4*w, i16_1 - i16_2);
            check(arm32 ? "vsub.i16" : "sub",  4*w, u16_1 - u16_2);
            check(arm32 ? "vsub.i32" : "sub",  2*w, i32_1 - i32_2);
            check(arm32 ? "vsub.i32" : "sub",  2*w, u32_1 - u32_2);
            check(arm32 ? "vsub.f32" : "fsub", 2*w, f32_1 - f32_2);

            // VSUBHN	I	-	Subtract and Narrow
            check(arm32 ? "vsubhn.i16" : "subhn", 8*w,  i8((i16_1 - i16_2)/256));
            check(arm32 ? "vsubhn.i16" : "subhn", 8*w,  u8((u16_1 - u16_2)/256));
            check(arm32 ? "vsubhn.i32" : "subhn", 4*w, i16((i32_1 - i32_2)/65536));
            check(arm32 ? "vsubhn.i32" : "subhn", 4*w, u16((u32_1 - u32_2)/65536));

            // VSUBL	I	-	Subtract Long
            check(arm32 ? "vsubl.s8"  : "ssubl", 8*w, i16(i8_1)  - i16(i8_2));
            check(arm32 ? "vsubl.u8"  : "usubl", 8*w, u16(u8_1)  - u16(u8_2));
            check(arm32 ? "vsubl.s16" : "ssubl", 4*w, i32(i16_1) - i32(i16_2));
            check(arm32 ? "vsubl.u16" : "usubl", 4*w, u32(u16_1) - u32(u16_2));
            check(arm32 ? "vsubl.s32" : "ssubl", 2*w, i64(i32_1) - i64(i32_2));
            check(arm32 ? "vsubl.u32" : "usubl", 2*w, u64(u32_1) - u64(u32_2));

            // VSUBW	I	-	Subtract Wide
            check(arm32 ? "vsubw.s8"  : "ssubw", 8*w, i16_1 - i8_1);
            check(arm32 ? "vsubw.u8"  : "usubw", 8*w, u16_1 - u8_1);
            check(arm32 ? "vsubw.s16" : "ssubw", 4*w, i32_1 - i16_1);
            check(arm32 ? "vsubw.u16" : "usubw", 4*w, u32_1 - u16_1);
            check(arm32 ? "vsubw.s32" : "ssubw", 2*w, i64_1 - i32_1);
            check(arm32 ? "vsubw.u32" : "usubw", 2*w, u64_1 - u32_1);

            // VST1	X	-	Store single-element structures
            check(arm32 ? "vst1.8" : "st", 8*w, i8_1);

        }

        // VST2	X	-	Store two-element structures
        for (int sign = 0; sign <= 1; sign++) {
            for (int width = 128; width <= 128*4; width *= 2) {
                for (int bits = 8; bits < 64; bits *= 2) {
                    if (width <= bits*2) continue;
                    Func tmp1, tmp2;
                    tmp1(x) = cast(sign ? Int(bits) : UInt(bits), x);
                    tmp1.compute_root();
                    tmp2(x, y) = select(x%2 == 0, tmp1(x/2), tmp1(x/2 + 16));
                    tmp2.compute_root().vectorize(x, width/bits);
                    string op = "vst2." + std::to_string(bits);
                    check(arm32 ? op : string("st2"), width/bits, tmp2(0, 0) + tmp2(0, 63));
                }
            }
        }

        // Also check when the two expressions interleaved have a common
        // subexpression, which results in a vector var being lifted out.
        for (int sign = 0; sign <= 1; sign++) {
            for (int width = 128; width <= 128*4; width *= 2) {
                for (int bits = 8; bits < 64; bits *= 2) {
                    if (width <= bits*2) continue;
                    Func tmp1, tmp2;
                    tmp1(x) = cast(sign ? Int(bits) : UInt(bits), x);
                    tmp1.compute_root();
                    Expr e = (tmp1(x/2)*2 + 7)/4;
                    tmp2(x, y) = select(x%2 == 0, e*3, e + 17);
                    tmp2.compute_root().vectorize(x, width/bits);
                    string op = "vst2." + std::to_string(bits);
                    check(arm32 ? op : string("st2"), width/bits, tmp2(0, 0) + tmp2(0, 127));
                }
            }
        }

        // VST3	X	-	Store three-element structures
        for (int sign = 0; sign <= 1; sign++) {
            for (int width = 192; width <= 192*4; width *= 2) {
                for (int bits = 8; bits < 64; bits *= 2) {
                    if (width <= bits*3) continue;
                    Func tmp1, tmp2;
                    tmp1(x) = cast(sign ? Int(bits) : UInt(bits), x);
                    tmp1.compute_root();
                    tmp2(x, y) = select(x%3 == 0, tmp1(x/3),
                                        x%3 == 1, tmp1(x/3 + 16),
                                        tmp1(x/3 + 32));
                    tmp2.compute_root().vectorize(x, width/bits);
                    string op = "vst3." + std::to_string(bits);
                    check(arm32 ? op : string("st3"), width/bits, tmp2(0, 0) + tmp2(0, 127));
                }
            }
        }

        // VST4	X	-	Store four-element structures
        for (int sign = 0; sign <= 1; sign++) {
            for (int width = 256; width <= 256*4; width *= 2) {
                for (int bits = 8; bits < 64; bits *= 2) {
                    if (width <= bits*4) continue;
                    Func tmp1, tmp2;
                    tmp1(x) = cast(sign ? Int(bits) : UInt(bits), x);
                    tmp1.compute_root();
                    tmp2(x, y) = select(x%4 == 0, tmp1(x/4),
                                        x%4 == 1, tmp1(x/4 + 16),
                                        x%4 == 2, tmp1(x/4 + 32),
                                        tmp1(x/4 + 48));
                    tmp2.compute_root().vectorize(x, width/bits);
                    string op = "vst4." + std::to_string(bits);
                    check(arm32 ? op : string("st4"), width/bits, tmp2(0, 0) + tmp2(0, 127));
                }
            }
        }

        // VSTM	X	F, D	Store Multiple Registers
        // VSTR	X	F, D	Store Register
        // we trust llvm to use these

        // VSWP	I	-	Swap Contents
        // Swaps the contents of two registers. Not sure why this would be useful.

        // VTBL	X	-	Table Lookup
        // Arm's version of shufps. Allows for arbitrary permutations of a
        // 64-bit vector. We typically use vrev variants instead.

        // VTBX	X	-	Table Extension
        // Like vtbl, but doesn't change any elements where the index was
        // out of bounds. Not sure how we'd use this.

        // VTRN	X	-	Transpose
        // Swaps the even elements of one vector with the odd elements of
        // another. Not useful for us.

        // VTST	I	-	Test Bits
        // check("vtst.32", 4, (bool1 & bool2) != 0);

        // VUZP	X	-	Unzip
        // VZIP	X	-	Zip
        // Interleave or deinterleave two vectors. Given that we use
        // interleaving loads and stores, it's hard to hit this op with
        // halide.
    }

    void check_hvx_all() {
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x+16), f32_3 = in_f32(x+32);
        Expr f64_1 = in_f64(x), f64_2 = in_f64(x+16), f64_3 = in_f64(x+32);
        Expr i8_1  = in_i8(x),  i8_2  = in_i8(x+16),  i8_3  = in_i8(x+32), i8_4 = in_i8(x + 48);
        Expr u8_1  = in_u8(x),  u8_2  = in_u8(x+16),  u8_3  = in_u8(x+32), u8_4 = in_u8(x + 48);
        Expr u8_even = in_u8(2*x), u8_odd = in_u8(2*x+1);
        Expr i16_1 = in_i16(x), i16_2 = in_i16(x+16), i16_3 = in_i16(x+32);
        Expr u16_1 = in_u16(x), u16_2 = in_u16(x+16), u16_3 = in_u16(x+32);
        Expr i32_1 = in_i32(x), i32_2 = in_i32(x+16), i32_3 = in_i32(x+32);
        Expr u32_1 = in_u32(x), u32_2 = in_u32(x+16), u32_3 = in_u32(x+32);
        Expr i64_1 = in_i64(x), i64_2 = in_i64(x+16), i64_3 = in_i64(x+32);
        Expr u64_1 = in_u64(x), u64_2 = in_u64(x+16), u64_3 = in_u64(x+32);
        Expr bool_1 = (f32_1 > 0.3f), bool_2 = (f32_1 < -0.3f), bool_3 = (f32_1 != -0.34f);

        int hvx_width = 0;
        if (target.has_feature(Target::HVX_64)) {
            hvx_width = 64;
        } else if (target.has_feature(Target::HVX_128)) {
            hvx_width = 128;
        }

        // Verify that unaligned loads use the right instructions, and don't try to use
        // immediates of more than 3 bits.
        check("valign(v*,v*,#7)", hvx_width/1, in_u8(x + 7));
        check("vlalign(v*,v*,#7)", hvx_width/1, in_u8(x + hvx_width - 7));
        check("valign(v*,v*,r*)", hvx_width/1, in_u8(x + 8));
        check("valign(v*,v*,r*)", hvx_width/1, in_u8(x + hvx_width - 8));
        check("valign(v*,v*,#6)", hvx_width/1, in_u16(x + 3));
        check("vlalign(v*,v*,#6)", hvx_width/1, in_u16(x + hvx_width - 3));
        check("valign(v*,v*,r*)", hvx_width/1, in_u16(x + 4));
        check("valign(v*,v*,r*)", hvx_width/1, in_u16(x + hvx_width - 4));

        check("vunpack(v*.ub)", hvx_width/1, u16(u8_1));
        check("vunpack(v*.ub)", hvx_width/1, i16(u8_1));
        check("vunpack(v*.uh)", hvx_width/2, u32(u16_1));
        check("vunpack(v*.uh)", hvx_width/2, i32(u16_1));
        check("vunpack(v*.b)", hvx_width/1, u16(i8_1));
        check("vunpack(v*.b)", hvx_width/1, i16(i8_1));
        check("vunpack(v*.h)", hvx_width/2, u32(i16_1));
        check("vunpack(v*.h)", hvx_width/2, i32(i16_1));

        check("vunpack(v*.ub)", hvx_width/1, u32(u8_1));
        check("vunpack(v*.ub)", hvx_width/1, i32(u8_1));
        check("vunpack(v*.b)", hvx_width/1, u32(i8_1));
        check("vunpack(v*.b)", hvx_width/1, i32(i8_1));

    #if 0
        // It's quite difficult to write a single expression that tests vzxt
        // and vsxt, because it gets rewritten as vpack/vunpack.
        check("vzxt(v*.ub)", hvx_width/1, u16(u8_1));
        check("vzxt(v*.ub)", hvx_width/1, i16(u8_1));
        check("vzxt(v*.uh)", hvx_width/2, u32(u16_1));
        check("vzxt(v*.uh)", hvx_width/2, i32(u16_1));
        check("vsxt(v*.b)", hvx_width/1, u16(i8_1));
        check("vsxt(v*.b)", hvx_width/1, i16(i8_1));
        check("vsxt(v*.h)", hvx_width/2, u32(i16_1));
        check("vsxt(v*.h)", hvx_width/2, i32(i16_1));

        check("vzxt(v*.ub)", hvx_width/1, u32(u8_1));
        check("vzxt(v*.ub)", hvx_width/1, i32(u8_1));
        check("vsxt(v*.b)", hvx_width/1, u32(i8_1));
        check("vsxt(v*.b)", hvx_width/1, i32(i8_1));
    #endif

        check("vadd(v*.b,v*.b)", hvx_width/1, u8_1 + u8_2);
        check("vadd(v*.h,v*.h)", hvx_width/2, u16_1 + u16_2);
        check("vadd(v*.w,v*.w)", hvx_width/4, u32_1 + u32_2);
        check("vadd(v*.b,v*.b)", hvx_width/1, i8_1 + i8_2);
        check("vadd(v*.h,v*.h)", hvx_width/2, i16_1 + i16_2);
        check("vadd(v*.w,v*.w)", hvx_width/4, i32_1 + i32_2);
        check("v*.h = vadd(v*.ub,v*.ub)", hvx_width/1, u16(u8_1) + u16(u8_2));
        check("v*.w = vadd(v*.uh,v*.uh)", hvx_width/2, u32(u16_1) + u32(u16_2));
        check("v*.w = vadd(v*.h,v*.h)", hvx_width/2, i32(i16_1) + i32(i16_2));
        check("vadd(v*.ub,v*.ub):sat", hvx_width/1, u8_sat(u16(u8_1 + u16(u8_2))));
        check("vadd(v*.uh,v*.uh):sat", hvx_width/2, u16_sat(u32(u16_1 + u32(u16_2))));
        check("vadd(v*.h,v*.h):sat", hvx_width/2, i16_sat(i32(i16_1 + i32(i16_2))));
        check("vadd(v*.w,v*.w):sat", hvx_width/4, i32_sat(i64(i32_1 + i64(i32_2))));

        check("vsub(v*.b,v*.b)", hvx_width/1, u8_1 - u8_2);
        check("vsub(v*.h,v*.h)", hvx_width/2, u16_1 - u16_2);
        check("vsub(v*.w,v*.w)", hvx_width/4, u32_1 - u32_2);
        check("vsub(v*.b,v*.b)", hvx_width/1, i8_1 - i8_2);
        check("vsub(v*.h,v*.h)", hvx_width/2, i16_1 - i16_2);
        check("vsub(v*.w,v*.w)", hvx_width/4, i32_1 - i32_2);
        check("v*.h = vsub(v*.ub,v*.ub)", hvx_width/1, u16(u8_1) - u16(u8_2));
        check("v*.w = vsub(v*.uh,v*.uh)", hvx_width/2, u32(u16_1) - u32(u16_2));
        check("v*.w = vsub(v*.h,v*.h)", hvx_width/2, i32(i16_1) - i32(i16_2));
        check("vsub(v*.ub,v*.ub):sat", hvx_width/1, u8_sat(i16(u8_1 - i16(u8_2))));
        check("vsub(v*.uh,v*.uh):sat", hvx_width/2, u16_sat(i32(u16_1 - i32(u16_2))));
        check("vsub(v*.h,v*.h):sat", hvx_width/2, i16_sat(i32(i16_1 - i32(i16_2))));
        check("vsub(v*.w,v*.w):sat", hvx_width/4, i32_sat(i64(i32_1 - i64(i32_2))));

        // Double vector versions of the above
        check("vadd(v*:*.b,v*:*.b)", hvx_width*2, u8_1 + u8_2);
        check("vadd(v*:*.h,v*:*.h)", hvx_width/1, u16_1 + u16_2);
        check("vadd(v*:*.w,v*:*.w)", hvx_width/2, u32_1 + u32_2);
        check("vadd(v*:*.b,v*:*.b)", hvx_width*2, i8_1 + i8_2);
        check("vadd(v*:*.h,v*:*.h)", hvx_width/1, i16_1 + i16_2);
        check("vadd(v*:*.w,v*:*.w)", hvx_width/2, i32_1 + i32_2);
        check("vadd(v*:*.ub,v*:*.ub):sat", hvx_width*2, u8_sat(u16(u8_1 + u16(u8_2))));
        check("vadd(v*:*.uh,v*:*.uh):sat", hvx_width/1, u16_sat(u32(u16_1 + u32(u16_2))));
        check("vadd(v*:*.h,v*:*.h):sat", hvx_width/1, i16_sat(i32(i16_1 + i32(i16_2))));
        check("vadd(v*:*.w,v*:*.w):sat", hvx_width/2, i32_sat(i64(i32_1 + i64(i32_2))));

        check("vsub(v*:*.b,v*:*.b)", hvx_width*2, u8_1 - u8_2);
        check("vsub(v*:*.h,v*:*.h)", hvx_width/1, u16_1 - u16_2);
        check("vsub(v*:*.w,v*:*.w)", hvx_width/2, u32_1 - u32_2);
        check("vsub(v*:*.b,v*:*.b)", hvx_width*2, i8_1 - i8_2);
        check("vsub(v*:*.h,v*:*.h)", hvx_width/1, i16_1 - i16_2);
        check("vsub(v*:*.w,v*:*.w)", hvx_width/2, i32_1 - i32_2);
        check("vsub(v*:*.ub,v*:*.ub):sat", hvx_width*2, u8_sat(i16(u8_1 - i16(u8_2))));
        check("vsub(v*:*.uh,v*:*.uh):sat", hvx_width/1, u16_sat(i32(u16_1 - i32(u16_2))));
        check("vsub(v*:*.h,v*:*.h):sat", hvx_width/1, i16_sat(i32(i16_1 - i32(i16_2))));
        check("vsub(v*:*.w,v*:*.w):sat", hvx_width/2, i32_sat(i64(i32_1 - i64(i32_2))));

        check("vavg(v*.ub,v*.ub)", hvx_width/1, u8((u16(u8_1) + u16(u8_2))/2));
        check("vavg(v*.ub,v*.ub):rnd", hvx_width/1, u8((u16(u8_1) + u16(u8_2) + 1)/2));
        check("vavg(v*.uh,v*.uh)", hvx_width/2, u16((u32(u16_1) + u32(u16_2))/2));
        check("vavg(v*.uh,v*.uh):rnd", hvx_width/2, u16((u32(u16_1) + u32(u16_2) + 1)/2));
        check("vavg(v*.h,v*.h)", hvx_width/2, i16((i32(i16_1) + i32(i16_2))/2));
        check("vavg(v*.h,v*.h):rnd", hvx_width/2, i16((i32(i16_1) + i32(i16_2) + 1)/2));
        check("vavg(v*.w,v*.w)", hvx_width/4, i32((i64(i32_1) + i64(i32_2))/2));
        check("vavg(v*.w,v*.w):rnd", hvx_width/4, i32((i64(i32_1) + i64(i32_2) + 1)/2));
        check("vnavg(v*.ub,v*.ub)", hvx_width/1, i8_sat((i16(u8_1) - i16(u8_2))/2));
        check("vnavg(v*.h,v*.h)", hvx_width/2, i16_sat((i32(i16_1) - i32(i16_2))/2));
        check("vnavg(v*.w,v*.w)", hvx_width/4, i32_sat((i64(i32_1) - i64(i32_2))/2));

        // The behavior of shifts larger than the type behave differently
        // on HVX vs. the scalar processor, so we clamp.
        check("vlsr(v*.h,v*.h)", hvx_width/1, u8_1 >> (u8_2 % 8));
        check("vlsr(v*.h,v*.h)", hvx_width/2, u16_1 >> (u16_2 % 16));
        check("vlsr(v*.w,v*.w)", hvx_width/4, u32_1 >> (u32_2 % 32));
        check("vasr(v*.h,v*.h)", hvx_width/1, i8_1 >> (i8_2 % 8));
        check("vasr(v*.h,v*.h)", hvx_width/2, i16_1 >> (i16_2 % 16));
        check("vasr(v*.w,v*.w)", hvx_width/4, i32_1 >> (i32_2 % 32));
        check("vasr(v*.h,v*.h,r*):sat", hvx_width/1, u8_sat(i16_1 >> 4));
        check("vasr(v*.w,v*.w,r*):sat", hvx_width/2, u16_sat(i32_1 >> 8));
        check("vasr(v*.w,v*.w,r*):sat", hvx_width/2, i16_sat(i32_1 >> 8));
        check("vasr(v*.w,v*.w,r*)", hvx_width/2, i16(i32_1 >> 8));
        check("vasl(v*.h,v*.h)", hvx_width/1, u8_1 << (u8_2 % 8));
        check("vasl(v*.h,v*.h)", hvx_width/2, u16_1 << (u16_2 % 16));
        check("vasl(v*.w,v*.w)", hvx_width/4, u32_1 << (u32_2 % 32));
        check("vasl(v*.h,v*.h)", hvx_width/1, i8_1 << (i8_2 % 8));
        check("vasl(v*.h,v*.h)", hvx_width/2, i16_1 << (i16_2 % 16));
        check("vasl(v*.w,v*.w)", hvx_width/4, i32_1 << (i32_2 % 32));

        // The scalar lsr generates uh/uw arguments, while the vector
        // version just generates h/w.
        check("vlsr(v*.uh,r*)", hvx_width/1, u8_1 >> (u8(y) % 8));
        check("vlsr(v*.uh,r*)", hvx_width/2, u16_1 >> (u16(y) % 16));
        check("vlsr(v*.uw,r*)", hvx_width/4, u32_1 >> (u32(y) % 32));
        check("vasr(v*.h,r*)", hvx_width/1, i8_1 >> (i8(y) % 8));
        check("vasr(v*.h,r*)", hvx_width/2, i16_1 >> (i16(y) % 16));
        check("vasr(v*.w,r*)", hvx_width/4, i32_1 >> (i32(y) % 32));
        check("vasl(v*.h,r*)", hvx_width/1, u8_1 << (u8(y) % 8));
        check("vasl(v*.h,r*)", hvx_width/2, u16_1 << (u16(y) % 16));
        check("vasl(v*.w,r*)", hvx_width/4, u32_1 << (u32(y) % 32));
        check("vasl(v*.h,r*)", hvx_width/1, i8_1 << (i8(y) % 8));
        check("vasl(v*.h,r*)", hvx_width/2, i16_1 << (i16(y) % 16));
        check("vasl(v*.w,r*)", hvx_width/4, i32_1 << (i32(y) % 32));

        check("vpacke(v*.h,v*.h)", hvx_width/1, u8(u16_1));
        check("vpacke(v*.h,v*.h)", hvx_width/1, u8(i16_1));
        check("vpacke(v*.h,v*.h)", hvx_width/1, i8(u16_1));
        check("vpacke(v*.h,v*.h)", hvx_width/1, i8(i16_1));
        check("vpacke(v*.w,v*.w)", hvx_width/2, u16(u32_1));
        check("vpacke(v*.w,v*.w)", hvx_width/2, u16(i32_1));
        check("vpacke(v*.w,v*.w)", hvx_width/2, i16(u32_1));
        check("vpacke(v*.w,v*.w)", hvx_width/2, i16(i32_1));

        check("vpacko(v*.h,v*.h)", hvx_width/1, u8(u16_1 >> 8));
        check("vpacko(v*.h,v*.h)", hvx_width/1, u8(i16_1 >> 8));
        check("vpacko(v*.h,v*.h)", hvx_width/1, i8(u16_1 >> 8));
        check("vpacko(v*.h,v*.h)", hvx_width/1, i8(i16_1 >> 8));
        check("vpacko(v*.w,v*.w)", hvx_width/2, u16(u32_1 >> 16));
        check("vpacko(v*.w,v*.w)", hvx_width/2, u16(i32_1 >> 16));
        check("vpacko(v*.w,v*.w)", hvx_width/2, i16(u32_1 >> 16));
        check("vpacko(v*.w,v*.w)", hvx_width/2, i16(i32_1 >> 16));

        // vpack doesn't interleave its inputs, which means it doesn't
        // simplify with widening. This is preferable for when the
        // pipeline doesn't widen to begin with, as in the above
        // tests. However, if the pipeline does widen, we want to generate
        // different instructions that have a built in interleaving that
        // we can cancel with the deinterleaving from widening.
        check("vshuffe(v*.b,v*.b)", hvx_width/1, u8(u16(u8_1) * 127));
        check("vshuffe(v*.b,v*.b)", hvx_width/1, u8(i16(i8_1) * 63));
        check("vshuffe(v*.b,v*.b)", hvx_width/1, i8(u16(u8_1) * 127));
        check("vshuffe(v*.b,v*.b)", hvx_width/1, i8(i16(i8_1) * 63));
        check("vshuffe(v*.h,v*.h)", hvx_width/2, u16(u32(u16_1) * 32767));
        check("vshuffe(v*.h,v*.h)", hvx_width/2, u16(i32(i16_1) * 16383));
        check("vshuffe(v*.h,v*.h)", hvx_width/2, i16(u32(u16_1) * 32767));
        check("vshuffe(v*.h,v*.h)", hvx_width/2, i16(i32(i16_1) * 16383));

        check("vshuffo(v*.b,v*.b)", hvx_width/1, u8((u16(u8_1) * 127) >> 8));
        check("vshuffo(v*.b,v*.b)", hvx_width/1, u8((i16(i8_1) * 63) >> 8));
        check("vshuffo(v*.b,v*.b)", hvx_width/1, i8((u16(u8_1) * 127) >> 8));
        check("vshuffo(v*.b,v*.b)", hvx_width/1, i8((i16(i8_1) * 63) >> 8));
        check("vshuffo(v*.h,v*.h)", hvx_width/2, u16((u32(u16_1) * 32767) >> 16));
        check("vshuffo(v*.h,v*.h)", hvx_width/2, u16((i32(i16_1) * 16383) >> 16));
        check("vshuffo(v*.h,v*.h)", hvx_width/2, i16((u32(u16_1) * 32767) >> 16));
        check("vshuffo(v*.h,v*.h)", hvx_width/2, i16((i32(i16_1) * 16383) >> 16));

        check("vpacke(v*.h,v*.h)", hvx_width/1, in_u8(2*x));
        check("vpacke(v*.w,v*.w)", hvx_width/2, in_u16(2*x));
        check("vdeal(v*,v*,r*)", hvx_width/4, in_u32(2*x));
        check("vpacko(v*.h,v*.h)", hvx_width/1, in_u8(2*x + 1));
        check("vpacko(v*.w,v*.w)", hvx_width/2, in_u16(2*x + 1));
        check("vdeal(v*,v*,r*)", hvx_width/4, in_u32(2*x + 1));

        check("vlut32(v*.b,v*.b,r*)", hvx_width/1, in_u8(3*x/2));
        check("vlut16(v*.b,v*.h,r*)", hvx_width/2, in_u16(3*x/2));

        check("vlut32(v*.b,v*.b,r*)", hvx_width/1, in_u8(u8_1));
        check("vlut32(v*.b,v*.b,r*)", hvx_width/1, in_u8(clamp(u16_1, 0, 63)));
        check("vlut16(v*.b,v*.h,r*)", hvx_width/2, in_u16(u8_1));
        check("vlut16(v*.b,v*.h,r*)", hvx_width/2, in_u16(clamp(u16_1, 0, 15)));

        check("v*.ub = vpack(v*.h,v*.h):sat", hvx_width/1, u8_sat(i16_1));
        check("v*.b = vpack(v*.h,v*.h):sat", hvx_width/1, i8_sat(i16_1));
        check("v*.uh = vpack(v*.w,v*.w):sat", hvx_width/2, u16_sat(i32_1));
        check("v*.h = vpack(v*.w,v*.w):sat", hvx_width/2, i16_sat(i32_1));

        // vpack doesn't interleave its inputs, which means it doesn't
        // simplify with widening. This is preferable for when the
        // pipeline doesn't widen to begin with, as in the above
        // tests. However, if the pipeline does widen, we want to generate
        // different instructions that have a built in interleaving that
        // we can cancel with the deinterleaving from widening.
        check("v*.ub = vsat(v*.h,v*.h)", hvx_width/1, u8_sat(i16(i8_1) << 8));
        check("v*.uh = vasr(v*.w,v*.w,r*):sat", hvx_width/2, u16_sat(i32(i16_1) << 16));
        check("v*.h = vasr(v*.w,v*.w,r*):sat", hvx_width/2, u8_sat(i32(i16_1) >> 4));
        check("v*.h = vsat(v*.w,v*.w)", hvx_width/2, i16_sat(i32(i16_1) << 16));

        // Also check double saturating narrows.
        check("v*.ub = vpack(v*.h,v*.h):sat", hvx_width/1, u8_sat(i32_1));
        check("v*.b = vpack(v*.h,v*.h):sat", hvx_width/1, i8_sat(i32_1));
        check("v*.h = vsat(v*.w,v*.w)", hvx_width/1, u8_sat(i32(i16_1) << 8));

        check("vround(v*.h,v*.h)", hvx_width/1, u8_sat((i32(i16_1) + 128)/256));
        check("vround(v*.h,v*.h)", hvx_width/1, i8_sat((i32(i16_1) + 128)/256));
        check("vround(v*.w,v*.w)", hvx_width/2, u16_sat((i64(i32_1) + 32768)/65536));
        check("vround(v*.w,v*.w)", hvx_width/2, i16_sat((i64(i32_1) + 32768)/65536));

        check("vshuff(v*,v*,r*)", hvx_width*2, select((x%2) == 0, in_u8(x/2), in_u8((x+16)/2)));
        check("vshuff(v*,v*,r*)", hvx_width*2, select((x%2) == 0, in_i8(x/2), in_i8((x+16)/2)));
        check("vshuff(v*,v*,r*)", (hvx_width*2)/2, select((x%2) == 0, in_u16(x/2), in_u16((x+16)/2)));
        check("vshuff(v*,v*,r*)", (hvx_width*2)/2, select((x%2) == 0, in_i16(x/2), in_i16((x+16)/2)));
        check("vshuff(v*,v*,r*)", (hvx_width*2)/4, select((x%2) == 0, in_u32(x/2), in_u32((x+16)/2)));
        check("vshuff(v*,v*,r*)", (hvx_width*2)/4, select((x%2) == 0, in_i32(x/2), in_i32((x+16)/2)));

        check("vshuff(v*,v*,r*)", hvx_width*2, select((x%2) == 0, u8(x/2), u8(x/2)));
        check("vshuff(v*,v*,r*)", hvx_width*2, select((x%2) == 0, i8(x/2), i8(x/2)));
        check("vshuff(v*,v*,r*)", (hvx_width*2)/2, select((x%2) == 0, u16(x/2), u16(x/2)));
        check("vshuff(v*,v*,r*)", (hvx_width*2)/2, select((x%2) == 0, i16(x/2), i16(x/2)));
        check("vshuff(v*,v*,r*)", (hvx_width*2)/4, select((x%2) == 0, u32(x/2), u32(x/2)));
        check("vshuff(v*,v*,r*)", (hvx_width*2)/4, select((x%2) == 0, i32(x/2), i32(x/2)));

        check("vmax(v*.ub,v*.ub)", hvx_width/1, max(u8_1, u8_2));
        check("vmax(v*.uh,v*.uh)", hvx_width/2, max(u16_1, u16_2));
        check("vmax(v*.h,v*.h)", hvx_width/2, max(i16_1, i16_2));
        check("vmax(v*.w,v*.w)", hvx_width/4, max(i32_1, i32_2));

        check("vmin(v*.ub,v*.ub)", hvx_width/1, min(u8_1, u8_2));
        check("vmin(v*.uh,v*.uh)", hvx_width/2, min(u16_1, u16_2));
        check("vmin(v*.h,v*.h)", hvx_width/2, min(i16_1, i16_2));
        check("vmin(v*.w,v*.w)", hvx_width/4, min(i32_1, i32_2));

        check("vcmp.gt(v*.b,v*.b)", hvx_width/1, select(i8_1 < i8_2, i8_1, i8_2));
        check("vcmp.gt(v*.ub,v*.ub)", hvx_width/1, select(u8_1 < u8_2, u8_1, u8_2));
        check("vcmp.gt(v*.h,v*.h)", hvx_width/2, select(i16_1 < i16_2, i16_1, i16_2));
        check("vcmp.gt(v*.uh,v*.uh)", hvx_width/2, select(u16_1 < u16_2, u16_1, u16_2));
        check("vcmp.gt(v*.w,v*.w)", hvx_width/4, select(i32_1 < i32_2, i32_1, i32_2));
        check("vcmp.gt(v*.uw,v*.uw)", hvx_width/4, select(u32_1 < u32_2, u32_1, u32_2));

        check("vcmp.gt(v*.b,v*.b)", hvx_width/1, select(i8_1 > i8_2, i8_1, i8_2));
        check("vcmp.gt(v*.ub,v*.ub)", hvx_width/1, select(u8_1 > u8_2, u8_1, u8_2));
        check("vcmp.gt(v*.h,v*.h)", hvx_width/2, select(i16_1 > i16_2, i16_1, i16_2));
        check("vcmp.gt(v*.uh,v*.uh)", hvx_width/2, select(u16_1 > u16_2, u16_1, u16_2));
        check("vcmp.gt(v*.w,v*.w)", hvx_width/4, select(i32_1 > i32_2, i32_1, i32_2));
        check("vcmp.gt(v*.uw,v*.uw)", hvx_width/4, select(u32_1 > u32_2, u32_1, u32_2));

        check("vcmp.gt(v*.b,v*.b)", hvx_width/1, select(i8_1 <= i8_2, i8_1, i8_2));
        check("vcmp.gt(v*.ub,v*.ub)", hvx_width/1, select(u8_1 <= u8_2, u8_1, u8_2));
        check("vcmp.gt(v*.h,v*.h)", hvx_width/2, select(i16_1 <= i16_2, i16_1, i16_2));
        check("vcmp.gt(v*.uh,v*.uh)", hvx_width/2, select(u16_1 <= u16_2, u16_1, u16_2));
        check("vcmp.gt(v*.w,v*.w)", hvx_width/4, select(i32_1 <= i32_2, i32_1, i32_2));
        check("vcmp.gt(v*.uw,v*.uw)", hvx_width/4, select(u32_1 <= u32_2, u32_1, u32_2));

        check("vcmp.gt(v*.b,v*.b)", hvx_width/1, select(i8_1 >= i8_2, i8_1, i8_2));
        check("vcmp.gt(v*.ub,v*.ub)", hvx_width/1, select(u8_1 >= u8_2, u8_1, u8_2));
        check("vcmp.gt(v*.h,v*.h)", hvx_width/2, select(i16_1 >= i16_2, i16_1, i16_2));
        check("vcmp.gt(v*.uh,v*.uh)", hvx_width/2, select(u16_1 >= u16_2, u16_1, u16_2));
        check("vcmp.gt(v*.w,v*.w)", hvx_width/4, select(i32_1 >= i32_2, i32_1, i32_2));
        check("vcmp.gt(v*.uw,v*.uw)", hvx_width/4, select(u32_1 >= u32_2, u32_1, u32_2));

        check("vcmp.eq(v*.b,v*.b)", hvx_width/1, select(i8_1 == i8_2, i8_1, i8_2));
        check("vcmp.eq(v*.b,v*.b)", hvx_width/1, select(u8_1 == u8_2, u8_1, u8_2));
        check("vcmp.eq(v*.h,v*.h)", hvx_width/2, select(i16_1 == i16_2, i16_1, i16_2));
        check("vcmp.eq(v*.h,v*.h)", hvx_width/2, select(u16_1 == u16_2, u16_1, u16_2));
        check("vcmp.eq(v*.w,v*.w)", hvx_width/4, select(i32_1 == i32_2, i32_1, i32_2));
        check("vcmp.eq(v*.w,v*.w)", hvx_width/4, select(u32_1 == u32_2, u32_1, u32_2));

        check("vcmp.eq(v*.b,v*.b)", hvx_width/1, select(i8_1 != i8_2, i8_1, i8_2));
        check("vcmp.eq(v*.b,v*.b)", hvx_width/1, select(u8_1 != u8_2, u8_1, u8_2));
        check("vcmp.eq(v*.h,v*.h)", hvx_width/2, select(i16_1 != i16_2, i16_1, i16_2));
        check("vcmp.eq(v*.h,v*.h)", hvx_width/2, select(u16_1 != u16_2, u16_1, u16_2));
        check("vcmp.eq(v*.w,v*.w)", hvx_width/4, select(i32_1 != i32_2, i32_1, i32_2));
        check("vcmp.eq(v*.w,v*.w)", hvx_width/4, select(u32_1 != u32_2, u32_1, u32_2));

        check("vabsdiff(v*.ub,v*.ub)", hvx_width/1, absd(u8_1, u8_2));
        check("vabsdiff(v*.uh,v*.uh)", hvx_width/2, absd(u16_1, u16_2));
        check("vabsdiff(v*.h,v*.h)", hvx_width/2, absd(i16_1, i16_2));
        check("vabsdiff(v*.w,v*.w)", hvx_width/4, absd(i32_1, i32_2));

        check("vand(v*,v*)", hvx_width/1, u8_1 & u8_2);
        check("vand(v*,v*)", hvx_width/2, u16_1 & u16_2);
        check("vand(v*,v*)", hvx_width/4, u32_1 & u32_2);
        check("vor(v*,v*)", hvx_width/1, u8_1 | u8_2);
        check("vor(v*,v*)", hvx_width/2, u16_1 | u16_2);
        check("vor(v*,v*)", hvx_width/4, u32_1 | u32_2);
        check("vxor(v*,v*)", hvx_width/1, u8_1 ^ u8_2);
        check("vxor(v*,v*)", hvx_width/2, u16_1 ^ u16_2);
        check("vxor(v*,v*)", hvx_width/4, u32_1 ^ u32_2);
        check("vnot(v*)", hvx_width/1, ~u8_1);
        check("vnot(v*)", hvx_width/2, ~u16_1);
        check("vnot(v*)", hvx_width/4, ~u32_1);

        check("vsplat(r*)", hvx_width/1, in_u8(0));
        check("vsplat(r*)", hvx_width/2, in_u16(0));
        check("vsplat(r*)", hvx_width/4, in_u32(0));

        check("vmux(q*,v*,v*)", hvx_width/1, select(i8_1 == i8_2, i8_1, i8_2));
        check("vmux(q*,v*,v*)", hvx_width/2, select(i16_1 == i16_2, i16_1, i16_2));
        check("vmux(q*,v*,v*)", hvx_width/4, select(i32_1 == i32_2, i32_1, i32_2));

        check("vabs(v*.h)", hvx_width/2, abs(i16_1));
        check("vabs(v*.w)", hvx_width/4, abs(i32_1));

        check("vmpy(v*.ub,v*.ub)", hvx_width/1, u16(u8_1) * u16(u8_2));
        check("vmpy(v*.b,v*.b)", hvx_width/1, i16(i8_1) * i16(i8_2));
        check("vmpy(v*.uh,v*.uh)", hvx_width/2, u32(u16_1) * u32(u16_2));
        check("vmpy(v*.h,v*.h)", hvx_width/2, i32(i16_1) * i32(i16_2));
        check("vmpyi(v*.h,v*.h)", hvx_width/2, i16_1 * i16_2);
        check("vmpyio(v*.w,v*.h)", hvx_width/2, i32_1 * i32(i16_1));
        check("vmpyie(v*.w,v*.uh)", hvx_width/2, i32_1 * i32(u16_1));
        check("vmpy(v*.uh,v*.uh)", hvx_width/2, u32_1 * u32(u16_1));
        check("vmpyieo(v*.h,v*.h)", hvx_width/4, i32_1 * i32_2);
        // The inconsistency in the expected instructions here is
        // correct. For bytes, the unsigned value is first, for half
        // words, the signed value is first.
        check("vmpy(v*.ub,v*.b)", hvx_width/1, i16(u8_1) * i16(i8_2));
        check("vmpy(v*.h,v*.uh)", hvx_width/2, i32(u16_1) * i32(i16_2));
        check("vmpy(v*.ub,v*.b)", hvx_width/1, i16(i8_1) * i16(u8_2));
        check("vmpy(v*.h,v*.uh)", hvx_width/2, i32(i16_1) * i32(u16_2));

        check("vmpy(v*.ub,r*.b)", hvx_width/1, i16(u8_1) * 3);
        check("vmpy(v*.h,r*.h)", hvx_width/2, i32(i16_1) * 10);
        check("vmpy(v*.ub,r*.ub)", hvx_width/1, u16(u8_1) * 3);
        check("vmpy(v*.uh,r*.uh)", hvx_width/2, u32(u16_1) * 10);

        check("vmpy(v*.ub,r*.b)", hvx_width/1, 3*i16(u8_1));
        check("vmpy(v*.h,r*.h)", hvx_width/2, 10*i32(i16_1));
        check("vmpy(v*.ub,r*.ub)", hvx_width/1, 3*u16(u8_1));
        check("vmpy(v*.uh,r*.uh)", hvx_width/2, 10*u32(u16_1));

        check("vmpyi(v*.h,r*.b)", hvx_width/2, i16_1 * 127);
        check("vmpyi(v*.h,r*.b)", hvx_width/2, 127 * i16_1);
        check("vmpyi(v*.w,r*.h)", hvx_width/4, i32_1 * 32767);
        check("vmpyi(v*.w,r*.h)", hvx_width/4, 32767 * i32_1);

        check("v*.h += vmpyi(v*.h,v*.h)", hvx_width/2, i16_1 + i16_2*i16_3);

        check("v*.h += vmpyi(v*.h,r*.b)", hvx_width/2, i16_1 + i16_2 * 127);
        check("v*.w += vmpyi(v*.w,r*.h)", hvx_width/4, i32_1 + i32_2 * 32767);
        check("v*.h += vmpyi(v*.h,r*.b)", hvx_width/2, i16_1 + 127 * i16_2);
        check("v*.w += vmpyi(v*.w,r*.h)", hvx_width/4, i32_1 + 32767 * i32_2);

        check("v*.uh += vmpy(v*.ub,v*.ub)", hvx_width/1, u16_1 + u16(u8_1) * u16(u8_2));
        check("v*.uw += vmpy(v*.uh,v*.uh)", hvx_width/2, u32_1 + u32(u16_1) * u32(u16_2));
        check("v*.h += vmpy(v*.b,v*.b)", hvx_width/1, i16_1 + i16(i8_1) * i16(i8_2));
        check("v*.w += vmpy(v*.h,v*.h)", hvx_width/2, i32_1 + i32(i16_1) * i32(i16_2));

        check("v*.h += vmpy(v*.ub,v*.b)", hvx_width/1, i16_1 + i16(u8_1) * i16(i8_2));
        check("v*.w += vmpy(v*.h,v*.uh)", hvx_width/2, i32_1 + i32(i16_1) * i32(u16_2));
        check("v*.h += vmpy(v*.ub,v*.b)", hvx_width/1, i16_1 + i16(u8_1) * i16(i8_2));
        check("v*.w += vmpy(v*.h,v*.uh)", hvx_width/2, i32_1 + i32(i16_1) * i32(u16_2));

        check("v*.h += vmpy(v*.ub,v*.b)", hvx_width/1, i16_1 + i16(i8_1) * i16(u8_2));
        check("v*.w += vmpy(v*.h,v*.uh)", hvx_width/2, i32_1 + i32(u16_1) * i32(i16_2));
        check("v*.h += vmpy(v*.ub,v*.b)", hvx_width/1, i16_1 + i16(i8_1) * i16(u8_2));
        check("v*.w += vmpy(v*.h,v*.uh)", hvx_width/2, i32_1 + i32(u16_1) * i32(i16_2));

        check("v*.uh += vmpy(v*.ub,r*.ub)", hvx_width/1, u16_1 + u16(u8_1) * 255);
        check("v*.h += vmpy(v*.ub,r*.b)", hvx_width/1, i16_1 + i16(u8_1) * 127);
        check("v*.uw += vmpy(v*.uh,r*.uh)", hvx_width/2, u32_1 + u32(u16_1) * 65535);
        check("v*.uh += vmpy(v*.ub,r*.ub)", hvx_width/1, u16_1 + 255 * u16(u8_1));
        check("v*.h += vmpy(v*.ub,r*.b)", hvx_width/1, i16_1 + 127 * i16(u8_1));
        check("v*.uw += vmpy(v*.uh,r*.uh)", hvx_width/2, u32_1 + 65535 * u32(u16_1));

        check("v*.h += vmpy(v*.ub,r*.b)", hvx_width/1, i16_1 - i16(u8_1) * -127);
        check("v*.h += vmpyi(v*.h,r*.b)", hvx_width/2, i16_1 - i16_2 * -127);

        check("v*.w += vmpy(v*.h,r*.h)", hvx_width/1, i32_1 + i32(i16_1)*32767);
        check("v*.w += vmpy(v*.h,r*.h)", hvx_width/1, i32_1 + 32767*i32(i16_1));

        check("vmpy(v*.h,v*.h):<<1:rnd:sat", hvx_width/2, i16_sat((i32(i16_1)*i32(i16_2) + 16384)/32768));
        check("vmpy(v*.h,r*.h):<<1:sat", hvx_width/2, i16_sat((i32(i16_1)*32767)/32768));
        check("vmpy(v*.h,r*.h):<<1:sat", hvx_width/2, i16_sat((32767*i32(i16_1))/32768));
        check("vmpy(v*.h,r*.h):<<1:rnd:sat", hvx_width/2, i16_sat((i32(i16_1)*32767 + 16384)/32768));
        check("vmpy(v*.h,r*.h):<<1:rnd:sat", hvx_width/2, i16_sat((32767*i32(i16_1) + 16384)/32768));

        check("vmpyo(v*.w,v*.h)", hvx_width/4, i32((i64(i32_1)*i64(i32_2))/(i64(1) << 32)));
        check("vmpyo(v*.w,v*.h):<<1:sat", hvx_width/4, i32_sat((i64(i32_1)*i64(i32_2))/(i64(1) << 31)));
        check("vmpyo(v*.w,v*.h):<<1:rnd:sat", hvx_width/4, i32_sat((i64(i32_1)*i64(i32_2) + (1 << 30))/(i64(1) << 31)));

        check("vmpa(v*.ub,r*.b)", hvx_width/1, i16(u8_1)*127 + i16(u8_2)*-128);
        check("vmpa(v*.ub,r*.b)", hvx_width/1, i16(u8_1)*127 + 126*i16(u8_2));
        check("vmpa(v*.ub,r*.b)", hvx_width/1, -100*i16(u8_1) + 40*i16(u8_2));
        check("v*.h += vmpa(v*.ub,r*.b)", hvx_width/1, 2*i16(u8_1) + 3*i16(u8_2) + i16_1);

        check("vmpa(v*.h,r*.b)", hvx_width/2, i32(i16_1)*2 + i32(i16_2)*3);
        check("vmpa(v*.h,r*.b)", hvx_width/2, i32(i16_1)*2 + 3*i32(i16_2));
        check("vmpa(v*.h,r*.b)", hvx_width/2, 2*i32(i16_1) + 3*i32(i16_2));
        check("v*.w += vmpa(v*.h,r*.b)", hvx_width/2, 2*i32(i16_1) + 3*i32(i16_2) + i32_1);

        // We only generate vdmpy if the inputs are interleaved (otherwise we would use vmpa).
        check("vdmpy(v*.ub,r*.b)", hvx_width/2, i16(in_u8(2*x))*127 + i16(in_u8(2*x + 1))*-128);
        check("vdmpy(v*.h,r*.b)", hvx_width/4, i32(in_i16(2*x))*2 + i32(in_i16(2*x + 1))*3);
        check("v*.h += vdmpy(v*.ub,r*.b)", hvx_width/2, i16(in_u8(2*x))*120 + i16(in_u8(2*x + 1))*-50 + i16_1);
        check("v*.w += vdmpy(v*.h,r*.b)", hvx_width/4, i32(in_i16(2*x))*80 + i32(in_i16(2*x + 1))*33 + i32_1);

#if 0
        // These are incorrect because the two operands aren't
        // interleaved correctly.
        check("vdmpy(v*:*.ub,r*.b)", (hvx_width/2)*2, i16(in_u8(2*x))*2 + i16(in_u8(2*x + 1))*3);
        check("vdmpy(v*:*.h,r*.b)", (hvx_width/4)*2, i32(in_i16(2*x))*2 + i32(in_i16(2*x + 1))*3);
        check("v*:*.h += vdmpy(v*:*.ub,r*.b)", (hvx_width/2)*2, i16(in_u8(2*x))*2 + i16(in_u8(2*x + 1))*3 + i16_1);
        check("v*:*.w += vdmpy(v*:*.h,r*.b)", (hvx_width/4)*2, i32(in_i16(2*x))*2 + i32(in_i16(2*x + 1))*3 + i32_1);
#endif

        check("vrmpy(v*.ub,r*.ub)", hvx_width, u32(u8_1)*255 + u32(u8_2)*254 + u32(u8_3)*253 + u32(u8_4)*252);
        check("vrmpy(v*.ub,r*.b)", hvx_width, i32(u8_1)*127 + i32(u8_2)*-128 + i32(u8_3)*126 + i32(u8_4)*-127);
        check("v*.uw += vrmpy(v*.ub,r*.ub)", hvx_width, u32_1 + u32(u8_1)*2 + u32(u8_2)*3 + u32(u8_3)*4 + u32(u8_4)*5);
        check("v*.w += vrmpy(v*.ub,r*.b)", hvx_width, i32_1 + i32(u8_1)*2 + i32(u8_2)*-3 + i32(u8_3)*-4 + i32(u8_4)*5);

        // Check a few of these with implicit ones.
        check("vrmpy(v*.ub,r*.b)", hvx_width, i32(u8_1) + i32(u8_2)*-2 + i32(u8_3)*3 + i32(u8_4)*-4);
        check("v*.w += vrmpy(v*.ub,r*.b)", hvx_width, i32_1 + i32(u8_1) + i32(u8_2)*2 + i32(u8_3)*3 + i32(u8_4)*4);

        // We should also match this pattern.
        check("vrmpy(v*.ub,r*.ub)", hvx_width, u32(u16(u8_1)*255) + u32(u16(u8_2)*254) + u32(u16(u8_3)*253) + u32(u16(u8_4)*252));
        check("v*.w += vrmpy(v*.ub,r*.b)", hvx_width, i32_1 + i32(i16(u8_1)*2) + i32(i16(u8_2)*-3) + i32(i16(u8_3)*-4) + i32(i16(u8_4)*5));

        check("vrmpy(v*.ub,v*.ub)", hvx_width, u32(u8_1)*u8_1 + u32(u8_2)*u8_2 + u32(u8_3)*u8_3 + u32(u8_4)*u8_4);
        check("vrmpy(v*.b,v*.b)", hvx_width, i32(i8_1)*i8_1 + i32(i8_2)*i8_2 + i32(i8_3)*i8_3 + i32(i8_4)*i8_4);
        check("v*.uw += vrmpy(v*.ub,v*.ub)", hvx_width, u32_1 + u32(u8_1)*u8_1 + u32(u8_2)*u8_2 + u32(u8_3)*u8_3 + u32(u8_4)*u8_4);
check("v*.w += vrmpy(v*.b,v*.b)", hvx_width, i32_1 + i32(i8_1)*i8_1 + i32(i8_2)*i8_2 + i32(i8_3)*i8_3 + i32(i8_4)*i8_4);

#if 0
        // These don't generate yet because we don't support mixed signs yet.
        check("vrmpy(v*.ub,v*.b)", hvx_width, i32(u8_1)*i8_1) + i32(u8_2)*i8_2) + i32(u8_3)*i8_3 + i32(u8_4)*i8_4);
        check("v*.w += vrmpy(v*.ub,v*.b)", hvx_width, i32_1 + i32(u8_1)*i8_1 + i32(u8_2)*i8_2 + i32(u8_3)*i8_3 + i32(u8_4)*i8_4);
        check("vrmpy(v*.ub,v*.b)", hvx_width, i16(u8_1)*i8_1 + i16(u8_2)*i8_2 + i16(u8_3)*i8_3 + i16(u8_4)*i8_4);
#endif

        // These should also work with 16 bit results. However, it is
        // only profitable to do so if the interleave simplifies away.
        Expr u8_4x4[] = {
            in_u8(4*x + 0),
            in_u8(4*x + 1),
            in_u8(4*x + 2),
            in_u8(4*x + 3),
        };
        check("vrmpy(v*.ub,r*.b)", hvx_width/2, i16(u8_4x4[0])*127 + i16(u8_4x4[1])*126 + i16(u8_4x4[2])*-125 + i16(u8_4x4[3])*124);
        // Make sure it doesn't generate if the operands don't interleave.
        check("vmpa(v*.ub,r*.b)", hvx_width, i16(u8_1)*127 + i16(u8_2)*-126 + i16(u8_3)*125 + i16(u8_4)*124);

        check("v*.w += vasl(v*.w,r*)", hvx_width/4, u32_1 + (u32_2 * 8));
        check("v*.w += vasl(v*.w,r*)", hvx_width/4, i32_1 + (i32_2 * 8));
        check("v*.w += vasr(v*.w,r*)", hvx_width/4, i32_1 + (i32_2 / 8));

        check("v*.w += vasl(v*.w,r*)", hvx_width/4, i32_1 + (i32_2 << (y % 32)));
        check("v*.w += vasr(v*.w,r*)", hvx_width/4, i32_1 + (i32_2 >> (y % 32)));

        check("vcl0(v*.uh)", hvx_width/2, count_leading_zeros(u16_1));
        check("vcl0(v*.uw)", hvx_width/4, count_leading_zeros(u32_1));
        check("vnormamt(v*.h)", hvx_width/2, max(count_leading_zeros(i16_1), count_leading_zeros(~i16_1)));
        check("vnormamt(v*.w)", hvx_width/4, max(count_leading_zeros(i32_1), count_leading_zeros(~i32_1)));
        check("vpopcount(v*.h)", hvx_width/2, popcount(u16_1));
    }

    void check_altivec_all() {
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x+16), f32_3 = in_f32(x+32);
        Expr f64_1 = in_f64(x), f64_2 = in_f64(x+16), f64_3 = in_f64(x+32);
        Expr i8_1  = in_i8(x),  i8_2  = in_i8(x+16),  i8_3  = in_i8(x+32);
        Expr u8_1  = in_u8(x),  u8_2  = in_u8(x+16),  u8_3  = in_u8(x+32);
        Expr i16_1 = in_i16(x), i16_2 = in_i16(x+16), i16_3 = in_i16(x+32);
        Expr u16_1 = in_u16(x), u16_2 = in_u16(x+16), u16_3 = in_u16(x+32);
        Expr i32_1 = in_i32(x), i32_2 = in_i32(x+16), i32_3 = in_i32(x+32);
        Expr u32_1 = in_u32(x), u32_2 = in_u32(x+16), u32_3 = in_u32(x+32);
        Expr i64_1 = in_i64(x), i64_2 = in_i64(x+16), i64_3 = in_i64(x+32);
        Expr u64_1 = in_u64(x), u64_2 = in_u64(x+16), u64_3 = in_u64(x+32);
        //Expr bool_1 = (f32_1 > 0.3f), bool_2 = (f32_1 < -0.3f), bool_3 = (f32_1 != -0.34f);

        // Basic AltiVec SIMD instructions.
        for (int w = 1; w <= 4; w++) {
            // Vector Integer Add Instructions.
            check("vaddsbs", 16*w, i8_sat(i16( i8_1) + i16( i8_2)));
            check("vaddshs", 8*w, i16_sat(i32(i16_1) + i32(i16_2)));
            check("vaddsws", 4*w, i32_sat(i64(i32_1) + i64(i32_2)));
            check("vaddubm", 16*w, i8_1 +  i8_2);
            check("vadduhm", 8*w, i16_1 + i16_2);
            check("vadduwm", 4*w, i32_1 + i32_2);
            check("vaddubs", 16*w, u8(min(u16( u8_1) + u16( u8_2),  max_u8)));
            check("vadduhs", 8*w, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
            check("vadduws", 4*w, u32(min(u64(u32_1) + u64(u32_2), max_u32)));

            // Vector Integer Subtract Instructions.
            check("vsubsbs", 16*w, i8_sat(i16( i8_1) - i16( i8_2)));
            check("vsubshs", 8*w, i16_sat(i32(i16_1) - i32(i16_2)));
            check("vsubsws", 4*w, i32_sat(i64(i32_1) - i64(i32_2)));
            check("vsububm", 16*w, i8_1 -  i8_2);
            check("vsubuhm", 8*w, i16_1 - i16_2);
            check("vsubuwm", 4*w, i32_1 - i32_2);
            check("vsububs", 16*w, u8(max(i16( u8_1) - i16( u8_2), 0)));
            check("vsubuhs", 8*w, u16(max(i32(u16_1) - i32(u16_2), 0)));
            check("vsubuws", 4*w, u32(max(i64(u32_1) - i64(u32_2), 0)));

            // Vector Integer Average Instructions.
            check("vavgsb", 16*w,  i8((i16( i8_1) + i16( i8_2) + 1)/2));
            check("vavgub", 16*w,  u8((u16( u8_1) + u16( u8_2) + 1)/2));
            check("vavgsh",  8*w, i16((i32(i16_1) + i32(i16_2) + 1)/2));
            check("vavguh",  8*w, u16((u32(u16_1) + u32(u16_2) + 1)/2));
            check("vavgsw",  4*w, i32((i64(i32_1) + i64(i32_2) + 1)/2));
            check("vavguw",  4*w, u32((u64(u32_1) + u64(u32_2) + 1)/2));

            // Vector Integer Maximum and Minimum Instructions
            check("vmaxsb", 16*w, max( i8_1, i8_2));
            check("vmaxub", 16*w, max( u8_1, u8_2));
            check("vmaxsh",  8*w, max(i16_1, i16_2));
            check("vmaxuh",  8*w, max(u16_1, u16_2));
            check("vmaxsw",  4*w, max(i32_1, i32_2));
            check("vmaxuw",  4*w, max(u32_1, u32_2));
            check("vminsb", 16*w, min( i8_1, i8_2));
            check("vminub", 16*w, min( u8_1, u8_2));
            check("vminsh",  8*w, min(i16_1, i16_2));
            check("vminuh",  8*w, min(u16_1, u16_2));
            check("vminsw",  4*w, min(i32_1, i32_2));
            check("vminuw",  4*w, min(u32_1, u32_2));

            // Vector Floating-Point Arithmetic Instructions
            check(use_vsx ? "xvaddsp"   : "vaddfp",  4*w, f32_1 + f32_2);
            check(use_vsx ? "xvsubsp"   : "vsubfp",  4*w, f32_1 - f32_2);
            check(use_vsx ? "xvmaddasp" : "vmaddfp", 4*w, f32_1 * f32_2 + f32_3);
            // check("vnmsubfp", 4, f32_1 - f32_2 * f32_3);

            // Vector Floating-Point Maximum and Minimum Instructions
            check("vmaxfp", 4*w, max(f32_1, f32_2));
            check("vminfp", 4*w, min(f32_1, f32_2));
        }

        // Check these if target supports VSX.
        if (use_vsx) {
            for (int w = 1; w <= 4; w++) {
                // VSX Vector Floating-Point Arithmetic Instructions
                check("xvadddp",  2*w, f64_1 + f64_2);
                check("xvmuldp",  2*w, f64_1 * f64_2);
                check("xvsubdp",  2*w, f64_1 - f64_2);
                check("xvaddsp",  4*w, f32_1 + f32_2);
                check("xvmulsp",  4*w, f32_1 * f32_2);
                check("xvsubsp",  4*w, f32_1 - f32_2);
                check("xvmaxdp",  2*w, max(f64_1, f64_2));
                check("xvmindp",  2*w, min(f64_1, f64_2));
            }
        }

        // Check these if target supports POWER ISA 2.07 and above.
        // These also include new instructions in POWER ISA 2.06.
        if (use_power_arch_2_07) {
            for (int w = 1; w <= 4; w++) {
                check("vaddudm", 2*w, i64_1 + i64_2);
                check("vsubudm", 2*w, i64_1 - i64_2);

                check("vmaxsd",  2*w, max(i64_1, i64_2));
                check("vmaxud",  2*w, max(u64_1, u64_2));
                check("vminsd",  2*w, min(i64_1, i64_2));
                check("vminud",  2*w, min(u64_1, u64_2));
            }
        }
    }

    bool test_all() {
        // Queue up a bunch of tasks representing each test to run.
        if (target.arch == Target::X86) {
            check_sse_all();
        } else if (target.arch == Target::ARM) {
            check_neon_all();
        } else if (target.arch == Target::Hexagon) {
            check_hvx_all();
        } else if (target.arch == Target::POWERPC) {
            check_altivec_all();
        }

        Halide::Internal::ThreadPool<TestResult> pool(num_threads);
        std::vector<std::future<TestResult>> futures;
        for (const Task &task : tasks) {
            futures.push_back(pool.async([this, task]() {
                return check_one(task.op, task.name, task.vector_width, task.expr);
            }));
        }

        bool success = true;
        for (auto &f : futures) {
            const TestResult &result = f.get();
            std::cout << result.op << "\n";
            if (!result.error_msg.empty()) {
                std::cerr << result.error_msg;
                success = false;
            }
        }

        return success;
    }
};

int main(int argc, char **argv) {
    Test test;

    if (argc > 1) {
        test.filter = argv[1];
        num_threads = 1;
    }

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

    // Compile a runtime for this target, for use in the static test.
    compile_standalone_runtime(test.output_directory + "simd_op_check_runtime.o", test.target);

    if (!success) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
