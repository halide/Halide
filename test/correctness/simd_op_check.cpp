#include "Halide.h"
#include <regex>
#include <fstream>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

using namespace Halide;

// This tests that we can correctly generate all the simd ops
using std::vector;
using std::string;

bool failed = false;
Var x, y;

bool use_ssse3, use_sse41, use_sse42, use_avx, use_avx2;

std::regex filter(".*");

Target target;

int num_processes = 16;
int my_process_id = 0;

void check(string op, int vector_width, Expr e) {
    static int counter = 0;
    counter++;

    // Come up with a name for this test
    std::string name = std::string("test_") + op + Internal::unique_name('_');
    for (size_t i = 0; i < name.size(); i++) {
        if (name[i] == '.') name[i] = '_';
    }

    // Bail out after generating the unique_name, so that names are
    // unique across different processes and don't depend on filter
    // settings.
    if (!regex_match(op, filter)) return;
    if (counter % num_processes != my_process_id) return;

    // Define a vectorized Func that uses the pattern.
    Func f(name);
    f(x, y) = e;
    f.bound(x, 0, vector_width*100).vectorize(x, vector_width);
    f.compute_root();

    // Include a scalar version
    Func f_scalar("scalar_" + name);
    f_scalar(x, y) = e;
    f_scalar.bound(x, 0, vector_width*100);
    f_scalar.compute_root();

    // The output to the pipeline is the L1 difference as a double.
    RDom r(0, vector_width*100, 0, 100);
    Func error("error_" + name);
    error() = sum(abs(cast<double>(f(r.x, r.y)) - f_scalar(r.x, r.y)));

    vector<Argument> arg_types;
    arg_types.push_back(Argument("in_f32", Argument::InputBuffer, Float(32), 1));
    arg_types.push_back(Argument("in_f64", Argument::InputBuffer, Float(64), 1));
    arg_types.push_back(Argument("in_i8",  Argument::InputBuffer, Int(8),    1));
    arg_types.push_back(Argument("in_u8",  Argument::InputBuffer, UInt(8),   1));
    arg_types.push_back(Argument("in_i16", Argument::InputBuffer, Int(16),   1));
    arg_types.push_back(Argument("in_u16", Argument::InputBuffer, UInt(16),  1));
    arg_types.push_back(Argument("in_i32", Argument::InputBuffer, Int(32),   1));
    arg_types.push_back(Argument("in_u32", Argument::InputBuffer, UInt(32),  1));
    arg_types.push_back(Argument("in_i64", Argument::InputBuffer, Int(64),   1));
    arg_types.push_back(Argument("in_u64", Argument::InputBuffer, UInt(64),  1));


    {
        // Compile just the vector Func to assembly
        string asm_filename = "check_" + f.name() + ".s";
        f.compile_to_assembly(asm_filename, arg_types, target);

        vector<string> lines;

        std::ifstream asm_file;
        asm_file.open(asm_filename);

        // Skip lines until we hit "for test_*y"
        std::regex start("for .*.s0.y"), end("end for.*x.x"), the_op("[^_]" + op);
        string line;
        while (getline(asm_file, line)) {
            if (regex_search(line, start)) break;
        }

        bool found_it = false;

        // Accumulate lines until we hit "end for*x.x"
        while (getline(asm_file, line)) {
            if (regex_search(line, end)) break;
            lines.push_back(line);
            // Check for the op in question
            found_it |= regex_search(line, the_op);
        }

        if (!found_it) {
            std::ostringstream msg;
            msg << op << " did not generate. Instead we got:\n";
            for (string l : lines) {
                msg << l << "\n";
            }
            failed = true;
            std::cerr << msg.str();
        }
        asm_file.close();
    }

    // Also compile the error checking Func
    error.compile_to_file(std::string("test_") + f.name(), arg_types, target);

    // Add some code to the test driver.
    // ...
}

Expr i64(Expr e) {
    return cast(Int(64), e);
}

Expr u64(Expr e) {
    return cast(UInt(64), e);
}

Expr i32(Expr e) {
    return cast(Int(32), e);
}

Expr u32(Expr e) {
    return cast(UInt(32), e);
}

Expr i16(Expr e) {
    return cast(Int(16), e);
}

Expr u16(Expr e) {
    return cast(UInt(16), e);
}

Expr i8(Expr e) {
    return cast(Int(8), e);
}

Expr u8(Expr e) {
    return cast(UInt(8), e);
}

Expr f32(Expr e) {
    return cast(Float(32), e);
}

Expr f64(Expr e) {
    return cast(Float(64), e);
}

void check_sse_all() {
    ImageParam in_f32(Float(32), 1, "in_f32");
    ImageParam in_f64(Float(64), 1, "in_f64");
    ImageParam in_i8(Int(8), 1, "in_i8");
    ImageParam in_u8(UInt(8), 1, "in_u8");
    ImageParam in_i16(Int(16), 1, "in_i16");
    ImageParam in_u16(UInt(16), 1, "in_u16");
    ImageParam in_i32(Int(32), 1, "in_i32");
    ImageParam in_u32(UInt(32), 1, "in_u32");
    ImageParam in_i64(Int(64), 1, "in_i64");
    ImageParam in_u64(UInt(64), 1, "in_u64");

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

    const int min_i8 = -128, max_i8 = 127;
    const int min_i16 = -32768, max_i16 = 32767;
    //const int min_i32 = 0x80000000, max_i32 = 0x7fffffff;
    const int max_u8 = 255;
    const int max_u16 = 65535;

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

        check("paddsb",  8*w, i8(clamp(i16(i8_1) + i16(i8_2), min_i8, max_i8)));
        // Add a test with a constant as there was a bug on this.
        check("paddsb",  8*w, i8(clamp(i16(i8_1) + i16(3), min_i8, max_i8)));
        check("psubsb",  8*w, i8(clamp(i16(i8_1) - i16(i8_2), min_i8, max_i8)));
        check("paddusb", 8*w, u8(min(u16(u8_1) + u16(u8_2), max_u8)));
        check("psubusb", 8*w, u8(max(i16(u8_1) - i16(u8_2), 0)));

        check("paddsw",  4*w, i16(clamp(i32(i16_1) + i32(i16_2), min_i16, max_i16)));
        check("psubsw",  4*w, i16(clamp(i32(i16_1) - i32(i16_2), min_i16, max_i16)));
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


        check("pcmpeqb", 8*w, select(u8_1 == u8_2, u8(1), u8(2)));
        check("pcmpgtb", 8*w, select(u8_1 > u8_2, u8(1), u8(2)));
        check("pcmpeqw", 4*w, select(u16_1 == u16_2, u16(1), u16(2)));
        check("pcmpgtw", 4*w, select(u16_1 > u16_2, u16(1), u16(2)));
        check("pcmpeqd", 2*w, select(u32_1 == u32_2, u32(1), u32(2)));
        check("pcmpgtd", 2*w, select(u32_1 > u32_2, u32(1), u32(2)));

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

        check("rcpps", 2*w, fast_inverse(f32_2));
        check("sqrtps", 2*w, sqrt(f32_2));
        check("rsqrtps", 2*w, fast_inverse_sqrt(f32_2));
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

    // These guys get normalized to the integer versions for widths other than 128-bits
    // check("andnps", 4, bool_1 & (~bool_2));
    check("andps", 4, bool_1 & bool_2);
    check("orps", 4, bool_1 | bool_2);
    check("xorps", 4, bool_1 ^ bool_2);



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
        check("pmuludq", w, u64_1 * u64_2);

        check("packssdw", 4*w, i16(clamp(i32_1, min_i16, max_i16)));
        check("packsswb", 8*w, i8(clamp(i16_1, min_i8, max_i8)));
        check("packuswb", 8*w, u8(clamp(i16_1, 0, max_u8)));
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
            check("pmuludq", 2*w, u64(u32_1) * u64(u32_2));
            check("pmulld", 2*w, i32_1 * i32_2);

            check("blendvps", 2*w, select(f32_1 > 0.7f, f32_1, f32_2));
            check("blendvpd", w, select(f64_1 > cast<double>(0.7f), f64_1, f64_2));
            check("pblendvb", 8*w, select(u8_1 > 7, u8_1, u8_2));
            check("pblendvb", 8*w, select(u8_1 == 7, u8_1, u8_2));
            check("pblendvb", 8*w, select(u8_1 <= 7, i8_1, i8_2));

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
            check("packusdw", 4*w, u16(clamp(i32_1, 0, max_u16)));
        }
    }

    // SSE 4.2
    if (use_sse42) {
        check("pcmpgtq", 2, select(i64_1 > i64_2, i64(1), i64(2)));
    }

    // AVX
    if (use_avx) {
        check("vsqrtps", 8, sqrt(f32_1));
        check("vsqrtpd", 4, sqrt(f64_1));
        check("vrsqrtps", 8, fast_inverse_sqrt(f32_1));
        check("vrcpps", 8, fast_inverse(f32_1));

        /* Not implemented yet in the front-end
           check("vandnps", 8, bool1 & (!bool2));
           check("vandps", 8, bool1 & bool2);
           check("vorps", 8, bool1 | bool2);
           check("vxorps", 8, bool1 ^ bool2);
        */

        check("vaddps", 8, f32_1 + f32_2);
        check("vaddpd", 4, f64_1 + f64_2);
        check("vmulps", 8, f32_1 * f32_2);
        check("vmulpd", 4, f64_1 * f64_2);
        check("vsubps", 8, f32_1 - f32_2);
        check("vsubpd", 4, f64_1 - f64_2);
        // LLVM no longer generates division instruction when fast-math is on
        //check("vdivps", 8, f32_1 / f32_2);
        //check("vdivpd", 4, f64_1 / f64_2);
        check("vminps", 8, min(f32_1, f32_2));
        check("vminpd", 4, min(f64_1, f64_2));
        check("vmaxps", 8, max(f32_1, f32_2));
        check("vmaxpd", 4, max(f64_1, f64_2));
        check("vroundps", 8, round(f32_1));
        check("vroundpd", 4, round(f64_1));

        check("vcmpeqpd", 4, select(f64_1 == f64_2, 1.0f, 2.0f));
        //check("vcmpneqpd", 4, select(f64_1 != f64_2, 1.0f, 2.0f));
        //check("vcmplepd", 4, select(f64_1 <= f64_2, 1.0f, 2.0f));
        check("vcmpltpd", 4, select(f64_1 < f64_2, 1.0f, 2.0f));
        check("vcmpeqps", 8, select(f32_1 == f32_2, 1.0f, 2.0f));
        //check("vcmpneqps", 8, select(f32_1 != f32_2, 1.0f, 2.0f));
        //check("vcmpleps", 8, select(f32_1 <= f32_2, 1.0f, 2.0f));
        check("vcmpltps", 8, select(f32_1 < f32_2, 1.0f, 2.0f));

        check("vblendvps", 8, select(f32_1 > 0.7f, f32_1, f32_2));
        check("vblendvpd", 4, select(f64_1 > cast<double>(0.7f), f64_1, f64_2));

        check("vcvttps2dq", 8, i32(f32_1));
        check("vcvtdq2ps", 8, f32(i32_1));
        check("vcvttpd2dq", 8, i32(f64_1));
        check("vcvtdq2pd", 8, f64(i32_1));
        check("vcvtps2pd", 8, f64(f32_1));
        check("vcvtpd2ps", 8, f32(f64_1));

        // Newer llvms will just vpshufd straight from memory for reversed loads
        // check("vperm", 8, in_f32(100-x));
    }

    // AVX 2

    if (use_avx2) {
        check("vpaddb", 32, u8_1 + u8_2);
        check("vpsubb", 32, u8_1 - u8_2);
        check("vpaddsb", 32, i8(clamp(i16(i8_1) + i16(i8_2), min_i8, max_i8)));
        check("vpsubsb", 32, i8(clamp(i16(i8_1) - i16(i8_2), min_i8, max_i8)));
        check("vpaddusb", 32, u8(min(u16(u8_1) + u16(u8_2), max_u8)));
        check("vpsubusb", 32, u8(min(u16(u8_1) - u16(u8_2), max_u8)));
        check("vpaddw", 16, u16_1 + u16_2);
        check("vpsubw", 16, u16_1 - u16_2);
        check("vpaddsw", 16, i16(clamp(i32(i16_1) + i32(i16_2), min_i16, max_i16)));
        check("vpsubsw", 16, i16(clamp(i32(i16_1) - i32(i16_2), min_i16, max_i16)));
        check("vpaddusw", 16, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
        check("vpsubusw", 16, u16(min(u32(u16_1) - u32(u16_2), max_u16)));
        check("vpaddd", 8, i32_1 + i32_2);
        check("vpsubd", 8, i32_1 - i32_2);
        check("vpmulhw", 16, i16((i32(i16_1) * i32(i16_2)) / (256*256)));
        check("vpmulhw", 16, i16((i32(i16_1) * i32(i16_2)) >> 16));
        check("vpmullw", 16, i16_1 * i16_2);

        check("vpcmpeqb", 32, select(u8_1 == u8_2, u8(1), u8(2)));
        check("vpcmpgtb", 32, select(u8_1 > u8_2, u8(1), u8(2)));
        check("vpcmpeqw", 16, select(u16_1 == u16_2, u16(1), u16(2)));
        check("vpcmpgtw", 16, select(u16_1 > u16_2, u16(1), u16(2)));
        check("vpcmpeqd", 8, select(u32_1 == u32_2, u32(1), u32(2)));
        check("vpcmpgtd", 8, select(u32_1 > u32_2, u32(1), u32(2)));

        check("vpavgb", 32, u8((u16(u8_1) + u16(u8_2) + 1)/2));
        check("vpavgw", 16, u16((u32(u16_1) + u32(u16_2) + 1)/2));
        check("vpmaxsw", 16, max(i16_1, i16_2));
        check("vpminsw", 16, min(i16_1, i16_2));
        check("vpmaxub", 32, max(u8_1, u8_2));
        check("vpminub", 32, min(u8_1, u8_2));
        check("vpmulhuw", 16, i16((i32(i16_1) * i32(i16_2))/(256*256)));
        check("vpmulhuw", 16, i16((i32(i16_1) * i32(i16_2))>>16));

        check("vpaddq", 8, i64_1 + i64_2);
        check("vpsubq", 8, i64_1 - i64_2);
        check("vpmuludq", 8, u64_1 * u64_2);

        check("vpackssdw", 16, i16(clamp(i32_1, min_i16, max_i16)));
        check("vpacksswb", 32, i8(clamp(i16_1, min_i8, max_i8)));
        check("vpackuswb", 32, u8(clamp(i16_1, 0, max_u8)));

        check("vpabsb", 32, abs(i8_1));
        check("vpabsw", 16, abs(i16_1));
        check("vpabsd", 8, abs(i32_1));

        // llvm doesn't distinguish between signed and unsigned multiplies
        // check("vpmuldq", 8, i64(i32_1) * i64(i32_2));
        check("vpmuludq", 8, u64(u32_1) * u64(u32_2));
        check("vpmulld", 8, i32_1 * i32_2);

        check("vpblendvb", 32, select(u8_1 > 7, u8_1, u8_2));

        check("vpmaxsb", 32, max(i8_1, i8_2));
        check("vpminsb", 32, min(i8_1, i8_2));
        check("vpmaxuw", 16, max(u16_1, u16_2));
        check("vpminuw", 16, min(u16_1, u16_2));
        check("vpmaxud", 16, max(u32_1, u32_2));
        check("vpminud", 16, min(u32_1, u32_2));
        check("vpmaxsd", 8, max(i32_1, i32_2));
        check("vpminsd", 8, min(i32_1, i32_2));

        check("vpcmpeqq", 4, select(i64_1 == i64_2, i64(1), i64(2)));
        check("vpackusdw", 16, u16(clamp(i32_1, 0, max_u16)));
        check("vpcmpgtq", 4, select(i64_1 > i64_2, i64(1), i64(2)));
    }
}

void check_neon_all() {
    ImageParam in_f32(Float(32), 1, "in_f32");
    ImageParam in_f64(Float(64), 1, "in_f64");
    ImageParam in_i8(Int(8), 1, "in_i8");
    ImageParam in_u8(UInt(8), 1, "in_u8");
    ImageParam in_i16(Int(16), 1, "in_i16");
    ImageParam in_u16(UInt(16), 1, "in_u16");
    ImageParam in_i32(Int(32), 1, "in_i32");
    ImageParam in_u32(UInt(32), 1, "in_u32");
    ImageParam in_i64(Int(64), 1, "in_i64");
    ImageParam in_u64(UInt(64), 1, "in_u64");

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

    const int min_i8 = -128, max_i8 = 127;
    const int min_i16 = -32768, max_i16 = 32767;
    const int min_i32 = 0x80000000, max_i32 = 0x7fffffff;
    const int max_u8 = 255;
    const int max_u16 = 65535;
    Expr max_u32 = UInt(32).max();

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
        /* Halide flips these to less than instead
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
        */

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
        /* We currently don't do this.
           check("vext.8", 16, in_i8(x+1));
           check("vext.16", 8, in_i16(x+1));
           check("vext.32", 4, in_i32(x+1));
        */

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
        // These are vfp, not neon. They only work on scalars
        /*
          check("vnmla.f32", 4, -(f32_1 + f32_2*f32_3));
          check("vnmla.f64", 2, -(f64_1 + f64_2*f64_3));
          check("vnmls.f32", 4, -(f32_1 - f32_2*f32_3));
          check("vnmls.f64", 2, -(f64_1 - f64_2*f64_3));
          check("vnmul.f32", 4, -(f32_1*f32_2));
          check("vnmul.f64", 2, -(f64_1*f64_2));
        */

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
        /* Of questionable value. Catching abs calls is annoying, and the
         * slow path is only one more op (for the max). */
        /*
          check("vqabs.s8", 16, abs(max(i8_1, -max_i8)));
          check("vqabs.s8", 8, abs(max(i8_1, -max_i8)));
          check("vqabs.s16", 8, abs(max(i16_1, -max_i16)));
          check("vqabs.s16", 4, abs(max(i16_1, -max_i16)));
          check("vqabs.s32", 4, abs(max(i32_1, -max_i32)));
          check("vqabs.s32", 2, abs(max(i32_1, -max_i32)));
        */

        // VQADD    I       -       Saturating Add
        check(arm32 ? "vqadd.s8"  : "sqadd", 8*w,  i8(clamp(i16(i8_1)  + i16(i8_2),  min_i8,  max_i8)));
        check(arm32 ? "vqadd.s16" : "sqadd", 4*w, i16(clamp(i32(i16_1) + i32(i16_2), min_i16, max_i16)));
        check(arm32 ? "vqadd.s32" : "sqadd", 2*w, i32(clamp(i64(i32_1) + i64(i32_2), min_i32, max_i32)));

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
        check(arm32 ? "vqmovn.s16" : "sqxtn", 8*w,  i8(clamp(i16_1, min_i8,  max_i8)));
        check(arm32 ? "vqmovn.s32" : "sqxtn", 4*w, i16(clamp(i32_1, min_i16, max_i16)));
        check(arm32 ? "vqmovn.s64" : "sqxtn", 2*w, i32(clamp(i64_1, min_i32, max_i32)));
        check(arm32 ? "vqmovn.u16" : "uqxtn", 8*w,  u8(min(u16_1, max_u8)));
        check(arm32 ? "vqmovn.u32" : "uqxtn", 4*w, u16(min(u32_1, max_u16)));
        check(arm32 ? "vqmovn.u64" : "uqxtn", 2*w, u32(min(u64_1, max_u32)));

        // VQMOVUN  I       -       Saturating Move and Unsigned Narrow
        check(arm32 ? "vqmovun.s16" : "sqxtun", 8*w, u8(clamp(i16_1, 0, max_u8)));
        check(arm32 ? "vqmovun.s32" : "sqxtun", 4*w, u16(clamp(i32_1, 0, max_u16)));
        check(arm32 ? "vqmovun.s64" : "sqxtun", 2*w, u32(clamp(i64_1, 0, max_u32)));

        // VQNEG    I       -       Saturating Negate
        check(arm32 ? "vqneg.s8" : "sqneg",  8*w, -max(i8_1,  -max_i8));
        check(arm32 ? "vqneg.s16" : "sqneg", 4*w, -max(i16_1, -max_i16));
        check(arm32 ? "vqneg.s32" : "sqneg", 2*w, -max(i32_1, -max_i32));

        // VQRDMULH I       -       Saturating Rounding Doubling Multiply Returning High Half
        // VQRSHL   I       -       Saturating Rounding Shift Left
        // VQRSHRN  I       -       Saturating Rounding Shift Right Narrow
        // VQRSHRUN I       -       Saturating Rounding Shift Right Unsigned Narrow
        // We use the non-rounding form of these (at worst we do an extra add)

        // VQSHL    I       -       Saturating Shift Left
        check(arm32 ? "vqshl.s8"  : "sqshl", 8*w,  i8(clamp(i16(i8_1)*16,  min_i8,  max_i8)));
        check(arm32 ? "vqshl.s16" : "sqshl", 4*w, i16(clamp(i32(i16_1)*16, min_i16, max_i16)));
        check(arm32 ? "vqshl.s32" : "sqshl", 2*w, i32(clamp(i64(i32_1)*16, min_i32, max_i32)));
        check(arm32 ? "vqshl.u8"  : "uqshl",  8*w,  u8(min(u16(u8_1 )*16, max_u8)));
        check(arm32 ? "vqshl.u16" : "uqshl", 4*w, u16(min(u32(u16_1)*16, max_u16)));
        check(arm32 ? "vqshl.u32" : "uqshl", 2*w, u32(min(u64(u32_1)*16, max_u32)));

        // VQSHLU   I       -       Saturating Shift Left Unsigned
        check(arm32 ? "vqshlu.s8"  : "sqshlu", 8*w,  u8(clamp(i16(i8_1)*16,  0,  max_u8)));
        check(arm32 ? "vqshlu.s16" : "sqshlu", 4*w, u16(clamp(i32(i16_1)*16, 0, max_u16)));
        check(arm32 ? "vqshlu.s32" : "sqshlu", 2*w, u32(clamp(i64(i32_1)*16, 0, max_u32)));


        // VQSHRN   I       -       Saturating Shift Right Narrow
        // VQSHRUN  I       -       Saturating Shift Right Unsigned Narrow
        check(arm32 ? "vqshrn.s64"  : "sqshrn",  2*w, i32(clamp(i64_1/16, min_i32, max_i32)));
        check(arm32 ? "vqshrun.s64" : "sqshrun", 2*w, u32(clamp(i64_1/16, 0, max_u32)));
        check(arm32 ? "vqshrn.u16"  : "uqshrn", 8*w,  u8(min(u16_1/16, max_u8)));
        check(arm32 ? "vqshrn.u32"  : "uqshrn", 4*w, u16(min(u32_1/16, max_u16)));
        check(arm32 ? "vqshrn.u64"  : "uqshrn", 2*w, u32(min(u64_1/16, max_u32)));

        // VQSUB    I       -       Saturating Subtract
        check(arm32 ? "vqsub.s8"  : "sqsub", 8*w,  i8(clamp(i16(i8_1)  - i16(i8_2),  min_i8,  max_i8)));
        check(arm32 ? "vqsub.s16" : "sqsub", 4*w, i16(clamp(i32(i16_1) - i32(i16_2), min_i16, max_i16)));
        check(arm32 ? "vqsub.s32" : "sqsub", 2*w, i32(clamp(i64(i32_1) - i64(i32_2), min_i32, max_i32)));

        // N.B. Saturating subtracts are expressed by widening to a *signed* type
        check(arm32 ? "vqsub.u8"  : "uqsub",  8*w,  u8(clamp(i16(u8_1)  - i16(u8_2),  0, max_u8)));
        check(arm32 ? "vqsub.u16" : "uqsub", 4*w, u16(clamp(i32(u16_1) - i32(u16_2), 0, max_u16)));
        check(arm32 ? "vqsub.u32" : "uqsub", 2*w, u32(clamp(i64(u32_1) - i64(u32_2), 0, max_u32)));

        // VRADDHN  I       -       Rounding Add and Narrow Returning High Half
        /* No rounding ops
           check("vraddhn.i16", 8, i8((i16_1 + i16_2 + 128)/256));
           check("vraddhn.i16", 8, u8((u16_1 + u16_2 + 128)/256));
           check("vraddhn.i32", 4, i16((i32_1 + i32_2 + 32768)/65536));
           check("vraddhn.i32", 4, u16((u32_1 + u32_2 + 32768)/65536));
        */

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

int main(int argc, char **argv) {
    if (argc > 1) {
        num_processes = 1;
        filter = std::regex(argv[1]);
    }

    // If we're testing everything, fork into many processes
    vector<int> children;
    for (int i = 1; i < num_processes; i++) {
        int pid = fork();
        if (!pid) {
            // I'm a worker
            my_process_id = i;
            children.clear();
            break;
        } else {
            // I'm the master
            children.push_back(pid);
        }
    }

    target = get_target_from_environment();
    target.set_features({Target::NoAsserts, Target::NoBoundsQuery, Target::JIT});

    use_avx2 = target.has_feature(Target::AVX2);
    use_avx = use_avx2 || target.has_feature(Target::AVX);
    use_sse41 = use_avx || target.has_feature(Target::SSE41);

    // There's no separate target for SSSE3; we currently enable it in
    // lockstep with SSE4.1
    use_ssse3 = use_sse41;
    // There's no separate target for SSS4.2; we currently assume that
    // it should be used iff AVX is being used.
    use_sse42 = use_avx;
    if (target.arch == Target::X86) {
        check_sse_all();
    } else {
        check_neon_all();
    }

    // Wait for any children to terminate
    for (int child : children) {
        int child_status = 0;
        waitpid(child, &child_status, 0);
        if (child_status) failed = true;
    }

    if (!children.empty() && !failed) {
        printf("Success!\n");
    }

    return failed ? -1 : 0;

}
