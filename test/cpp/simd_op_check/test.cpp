#include "Halide.h"
#include <stdarg.h>
#include <string.h>

using namespace Halide;

// This tests that we can correctly generate all the simd ops

bool failed = false;
Var x;

void check(const char *op, int vector_width, Expr e, const char *args) {
    Func f;
    f(x) = e;
    f.vectorize(x, vector_width);
    char module[1024];
    snprintf(module, 1024, "test_%s_%s", op, f.name().c_str());
    f.compileToFile(module);
    const char *llc = "../../../llvm/Release+Asserts/bin/llc";
    char cmd[1024];
    snprintf(cmd, 1024, 
	     "%s %s %s.bc -o - | sed -n '/%s.v0_loop/,/%s.v0_afterloop/p' | grep '\t' > %s.s && grep %s %s.s", 
	     llc, args, module, f.name().c_str(), f.name().c_str(), module, op, module);

    if (system(cmd) != 0) {
	fprintf(stderr, "\n%s did not generate. Instead we got:\n", op);
	char asmfile[1024];
	snprintf(asmfile, 1024, "%s.s", module);
	FILE *f = fopen(asmfile, "r");
	const int max_size = 1024;
	char buf[max_size];
	memset(buf, 0, max_size);
	size_t bytes_in = fread(buf, 1, max_size, f);	
	if (bytes_in > max_size-1) {
	    buf[max_size-6] = ' ';
	    buf[max_size-5] = '.';
	    buf[max_size-4] = '.';
	    buf[max_size-3] = '.';
	    buf[max_size-2] = '\n';
	    buf[max_size-1] = 0;
	} else {
	    buf[bytes_in] = 0;
	}
	fwrite(buf, 1, bytes_in-1, stderr);	
	fclose(f);
	fprintf(stderr, "\n");
	failed = 1;
    } 
}

void check_sse(const char *op, int vector_width, Expr e) {
    check(op, vector_width, e, "-mattr=+avx,+avx2");
}

void check_neon(const char *op, int vector_width, Expr e) {
    check(op, vector_width, e, "-mattr=+neon");
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

Expr absd(Expr a, Expr b) {
    return select(a > b, a-b, b-a);
}

void check_sse_all() {
    UniformImage in_f32(Float(32), 1);
    UniformImage in_f64(Float(64), 1);
    UniformImage in_i8(Int(8), 1);
    UniformImage in_u8(UInt(8), 1);
    UniformImage in_i16(Int(16), 1);
    UniformImage in_u16(UInt(16), 1);
    UniformImage in_i32(Int(32), 1);
    UniformImage in_u32(UInt(32), 1);
    UniformImage in_i64(Int(64), 1);
    UniformImage in_u64(UInt(64), 1);

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

    // MMX (in 128-bits)
    check_sse("paddb", 16, u8_1 + u8_2);
    check_sse("psubb", 16, u8_1 - u8_2);
    check_sse("paddsb", 16, i8(clamp(i16(i8_1) + i16(i8_2), -128, 127)));
    check_sse("psubsb", 16, i8(clamp(i16(i8_1) - i16(i8_2), -128, 127)));
    check_sse("paddusb", 16, u8(clamp(u16(u8_1) + u16(u8_2), 0, 255)));
    check_sse("psubusb", 16, u8(clamp(u16(u8_1) - u16(u8_2), 0, 255)));
    check_sse("paddw", 8, u16_1 + u16_2);
    check_sse("psubw", 8, u16_1 - u16_2);
    check_sse("paddsw", 8, i16(clamp(i32(i16_1) + i32(i16_2), -32768, 32767)));
    check_sse("psubsw", 8, i16(clamp(i32(i16_1) - i32(i16_2), -32768, 32767)));
    check_sse("paddusw", 8, u16(clamp(u32(u16_1) + u32(u16_2), 0, 65535)));
    check_sse("psubusw", 8, u16(clamp(u32(u16_1) - u32(u16_2), 0, 65535)));
    check_sse("paddd", 4, i32_1 + i32_2);
    check_sse("psubd", 4, i32_1 - i32_2);
    check_sse("pmulhw", 8, i16((i32(i16_1) * i32(i16_2)) / (256*256)));
    check_sse("pmullw", 8, i16_1 * i16_2);

    check_sse("pcmpeqb", 16, select(u8_1 == u8_2, u8(1), u8(2)));
    check_sse("pcmpgtb", 16, select(u8_1 > u8_2, u8(1), u8(2)));
    check_sse("pcmpeqw", 8, select(u16_1 == u16_2, u16(1), u16(2)));
    check_sse("pcmpgtw", 8, select(u16_1 > u16_2, u16(1), u16(2)));
    check_sse("pcmpeqd", 4, select(u32_1 == u32_2, u32(1), u32(2)));
    check_sse("pcmpgtd", 4, select(u32_1 > u32_2, u32(1), u32(2)));
    

    // SSE 1
    check_sse("addps", 4, f32_1 + f32_2);
    check_sse("subps", 4, f32_1 - f32_2);
    check_sse("mulps", 4, f32_1 * f32_2);
    check_sse("divps", 4, f32_1 / f32_2);
    check_sse("rcpps", 4, 1.0f / f32_2);
    check_sse("sqrtps", 4, sqrt(f32_2));
    check_sse("rsqrtps", 4, 1.0f / sqrt(f32_2));
    check_sse("maxps", 4, max(f32_1, f32_2));
    check_sse("minps", 4, min(f32_1, f32_2));
    check_sse("pavgb", 16, (u8_1 + u8_2)/2);
    check_sse("pavgw", 8, (i16_1 + i16_2)/2);
    check_sse("pmaxsw", 8, max(i16_1, i16_2));
    check_sse("pminsw", 8, min(i16_1, i16_2));
    check_sse("pmaxub", 16, max(u8_1, u8_2));
    check_sse("pminub", 16, min(u8_1, u8_2));
    check_sse("pmulhuw", 8, i16((i32(i16_1) * i32(i16_2))/(256*256)));

    /* Not implemented yet in the front-end
    check_sse("andnps", 4, bool1 & (!bool2));
    check_sse("andps", 4, bool1 & bool2);
    check_sse("orps", 4, bool1 | bool2);    
    check_sse("xorps", 4, bool1 ^ bool2);    
    */

    check_sse("cmpeqps", 4, select(f32_1 == f32_2, 1.0f, 2.0f));
    check_sse("cmpneqps", 4, select(f32_1 != f32_2, 1.0f, 2.0f));
    check_sse("cmpleps", 4, select(f32_1 <= f32_2, 1.0f, 2.0f));
    check_sse("cmpltps", 4, select(f32_1 < f32_2, 1.0f, 2.0f));

    // These ones are not necessary, because we just flip the args and use the above two
    //check_sse("cmpnleps", 4, select(f32_1 > f32_2, 1.0f, 2.0f));
    //check_sse("cmpnltps", 4, select(f32_1 >= f32_2, 1.0f, 2.0f));

    check_sse("shufps", 4, in_f32(100-x));
    check_sse("shufps", 4, in_f32(2*x));

    // SSE 2

    check_sse("addpd", 2, f64_1 + f64_2);
    check_sse("subpd", 2, f64_1 - f64_2);
    check_sse("mulpd", 2, f64_1 * f64_2);
    check_sse("divpd", 2, f64_1 / f64_2);
    check_sse("sqrtpd", 2, sqrt(f64_2));
    check_sse("maxpd", 2, max(f64_1, f64_2));
    check_sse("minpd", 2, min(f64_1, f64_2));

    check_sse("cmpeqpd", 2, select(f64_1 == f64_2, 1.0f, 2.0f));
    check_sse("cmpneqpd", 2, select(f64_1 != f64_2, 1.0f, 2.0f));
    check_sse("cmplepd", 2, select(f64_1 <= f64_2, 1.0f, 2.0f));
    check_sse("cmpltpd", 2, select(f64_1 < f64_2, 1.0f, 2.0f));

    check_sse("cvttps2dq", 4, i32(f32_1));
    check_sse("cvtdq2ps", 4, f32(i32_1));    
    check_sse("cvttpd2dq", 4, i32(f64_1));
    check_sse("cvtdq2pd", 4, f64(i32_1));
    check_sse("cvtps2pd", 4, f64(f32_1));
    check_sse("cvtpd2ps", 4, f32(f64_1));

    check_sse("paddq", 4, i64_1 + i64_2);
    check_sse("psubq", 4, i64_1 - i64_2);
    check_sse("pmuludq", 4, u64_1 * u64_2);

    check_sse("packssdw", 8, i16(clamp(i32_1, -32768, 32767)));
    check_sse("packsswb", 16, i8(clamp(i16_1, -128, 127)));
    check_sse("packuswb", 16, u8(clamp(i16_1, 0, 255)));

    // SSE 3

    // We don't do horizontal add/sub ops, so nothing new here
    
    // SSSE 3
    check_sse("pabsb", 16, abs(i8_1));
    check_sse("pabsw", 8, abs(i16_1));
    check_sse("pabsd", 4, abs(i32_1));

    // SSE 4.1

    // skip dot product and argmin 

    check_sse("pmuldq", 2, i64(i32_1) * i64(i32_2));
    check_sse("pmulld", 4, i32_1 * i32_2);

    check_sse("blendvps", 4, select(f32_1 > 0.7f, f32_1, f32_2));
    check_sse("blendvpd", 2, select(f64_1 > 0.7, f64_1, f64_2));
    check_sse("pblendvb", 16, select(u8_1 > 7, u8_1, u8_2));

    check_sse("pmaxsb", 16, max(i8_1, i8_2));
    check_sse("pminsb", 16, min(i8_1, i8_2));
    check_sse("pmaxuw", 8, max(u16_1, u16_2));
    check_sse("pminuw", 8, min(u16_1, u16_2));
    check_sse("pmaxud", 8, max(u32_1, u32_2));
    check_sse("pminud", 8, min(u32_1, u32_2));
    check_sse("pmaxsd", 4, max(i32_1, i32_2));
    check_sse("pminsd", 4, min(i32_1, i32_2));

    check_sse("roundps", 4, round(f32_1));
    check_sse("roundpd", 2, round(f64_1));

    check_sse("pcmpeqq", 2, select(i64_1 == i64_2, i64(1), i64(2)));
    check_sse("packusdw", 8, u16(clamp(i32_1, 0, 65535)));

    // SSE 4.2
    
    check_sse("pcmpgtq", 2, select(i64_1 > i64_2, i64(1), i64(2)));

    // AVX
    check_sse("vsqrtps", 8, sqrt(f32_1));
    check_sse("vsqrtpd", 4, sqrt(f64_1));
    check_sse("vrsqrtps", 8, 1.0f/sqrt(f32_1));
    check_sse("vrcpps", 8, 1.0f/f32_1);

    /* Not implemented yet in the front-end
    check_sse("vandnps", 8, bool1 & (!bool2));
    check_sse("vandps", 8, bool1 & bool2);
    check_sse("vorps", 8, bool1 | bool2);    
    check_sse("vxorps", 8, bool1 ^ bool2);    
    */

    check_sse("vaddps", 8, f32_1 + f32_2);
    check_sse("vaddpd", 4, f64_1 + f64_2);
    check_sse("vmulps", 8, f32_1 * f32_2);
    check_sse("vmulpd", 4, f64_1 * f64_2);
    check_sse("vsubps", 8, f32_1 - f32_2);
    check_sse("vsubpd", 4, f64_1 - f64_2);
    check_sse("vdivps", 8, f32_1 / f32_2);
    check_sse("vdivpd", 4, f64_1 / f64_2);
    check_sse("vminps", 8, min(f32_1, f32_2));
    check_sse("vminpd", 4, min(f64_1, f64_2));
    check_sse("vmaxps", 8, max(f32_1, f32_2));
    check_sse("vmaxpd", 4, max(f64_1, f64_2));
    check_sse("vroundps", 8, round(f32_1));
    check_sse("vroundpd", 4, round(f64_1));
    
    check_sse("vcmpeqpd", 4, select(f64_1 == f64_2, 1.0f, 2.0f));
    check_sse("vcmpneqpd", 4, select(f64_1 != f64_2, 1.0f, 2.0f));
    check_sse("vcmplepd", 4, select(f64_1 <= f64_2, 1.0f, 2.0f));
    check_sse("vcmpltpd", 4, select(f64_1 < f64_2, 1.0f, 2.0f));
    check_sse("vcmpeqps", 8, select(f32_1 == f32_2, 1.0f, 2.0f));
    check_sse("vcmpneqps", 8, select(f32_1 != f32_2, 1.0f, 2.0f));
    check_sse("vcmpleps", 8, select(f32_1 <= f32_2, 1.0f, 2.0f));
    check_sse("vcmpltps", 8, select(f32_1 < f32_2, 1.0f, 2.0f));

    check_sse("vblendvps", 8, select(f32_1 > 0.7f, f32_1, f32_2));
    check_sse("vblendvpd", 4, select(f64_1 > 0.7, f64_1, f64_2));

    check_sse("vcvttps2dq", 8, i32(f32_1));
    check_sse("vcvtdq2ps", 8, f32(i32_1));    
    check_sse("vcvttpd2dq", 8, i32(f64_1));
    check_sse("vcvtdq2pd", 8, f64(i32_1));
    check_sse("vcvtps2pd", 8, f64(f32_1));
    check_sse("vcvtpd2ps", 8, f32(f64_1));

    // AVX 2
    check_sse("vpaddb", 32, u8_1 + u8_2);
    check_sse("vpsubb", 32, u8_1 - u8_2);
    check_sse("vpaddsb", 32, i8(clamp(i16(i8_1) + i16(i8_2), -128, 127)));
    check_sse("vpsubsb", 32, i8(clamp(i16(i8_1) - i16(i8_2), -128, 127)));
    check_sse("vpaddusb", 32, u8(clamp(u16(u8_1) + u16(u8_2), 0, 255)));
    check_sse("vpsubusb", 32, u8(clamp(u16(u8_1) - u16(u8_2), 0, 255)));
    check_sse("vpaddw", 16, u16_1 + u16_2);
    check_sse("vpsubw", 16, u16_1 - u16_2);
    check_sse("vpaddsw", 16, i16(clamp(i32(i16_1) + i32(i16_2), -32768, 32767)));
    check_sse("vpsubsw", 16, i16(clamp(i32(i16_1) - i32(i16_2), -32768, 32767)));
    check_sse("vpaddusw", 16, u16(clamp(u32(u16_1) + u32(u16_2), 0, 65535)));
    check_sse("vpsubusw", 16, u16(clamp(u32(u16_1) - u32(u16_2), 0, 65535)));
    check_sse("vpaddd", 8, i32_1 + i32_2);
    check_sse("vpsubd", 8, i32_1 - i32_2);
    check_sse("vpmulhw", 16, i16((i32(i16_1) * i32(i16_2)) / (256*256)));
    check_sse("vpmullw", 16, i16_1 * i16_2);

    check_sse("vpcmpeqb", 32, select(u8_1 == u8_2, u8(1), u8(2)));
    check_sse("vpcmpgtb", 32, select(u8_1 > u8_2, u8(1), u8(2)));
    check_sse("vpcmpeqw", 16, select(u16_1 == u16_2, u16(1), u16(2)));
    check_sse("vpcmpgtw", 16, select(u16_1 > u16_2, u16(1), u16(2)));
    check_sse("vpcmpeqd", 8, select(u32_1 == u32_2, u32(1), u32(2)));
    check_sse("vpcmpgtd", 8, select(u32_1 > u32_2, u32(1), u32(2)));    
    
    check_sse("vpavgb", 32, (u8_1 + u8_2)/2);
    check_sse("vpavgw", 16, (i16_1 + i16_2)/2);
    check_sse("vpmaxsw", 16, max(i16_1, i16_2));
    check_sse("vpminsw", 16, min(i16_1, i16_2));
    check_sse("vpmaxub", 32, max(u8_1, u8_2));
    check_sse("vpminub", 32, min(u8_1, u8_2));
    check_sse("vpmulhuw", 16, i16((i32(i16_1) * i32(i16_2))/(256*256)));

    check_sse("vpaddq", 8, i64_1 + i64_2);
    check_sse("vpsubq", 8, i64_1 - i64_2);
    check_sse("vpmuludq", 8, u64_1 * u64_2);

    check_sse("vpackssdw", 16, i16(clamp(i32_1, -32768, 32767)));
    check_sse("vpacksswb", 32, i8(clamp(i16_1, -128, 127)));
    check_sse("vpackuswb", 32, u8(clamp(i16_1, 0, 255)));

    check_sse("vpabsb", 32, abs(i8_1));
    check_sse("vpabsw", 16, abs(i16_1));
    check_sse("vpabsd", 8, abs(i32_1));

    check_sse("vpmuldq", 4, i64(i32_1) * i64(i32_2));
    check_sse("vpmulld", 8, i32_1 * i32_2);

    check_sse("vpblendvb", 32, select(u8_1 > 7, u8_1, u8_2));

    check_sse("vpmaxsb", 32, max(i8_1, i8_2));
    check_sse("vpminsb", 32, min(i8_1, i8_2));
    check_sse("vpmaxuw", 16, max(u16_1, u16_2));
    check_sse("vpminuw", 16, min(u16_1, u16_2));
    check_sse("vpmaxud", 16, max(u32_1, u32_2));
    check_sse("vpminud", 16, min(u32_1, u32_2));
    check_sse("vpmaxsd", 8, max(i32_1, i32_2));
    check_sse("vpminsd", 8, min(i32_1, i32_2));

    check_sse("vpcmpeqq", 4, select(i64_1 == i64_2, i64(1), i64(2)));
    check_sse("vpackusdw", 16, u16(clamp(i32_1, 0, 65535)));
    check_sse("vpcmpgtq", 4, select(i64_1 > i64_2, i64(1), i64(2)));

}

void check_neon_all() {
    UniformImage in_f32(Float(32), 1);
    UniformImage in_f64(Float(64), 1);
    UniformImage in_i8(Int(8), 1);
    UniformImage in_u8(UInt(8), 1);
    UniformImage in_i16(Int(16), 1);
    UniformImage in_u16(UInt(16), 1);
    UniformImage in_i32(Int(32), 1);
    UniformImage in_u32(UInt(32), 1);
    UniformImage in_i64(Int(64), 1);
    UniformImage in_u64(UInt(64), 1);

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
    // 64-bit args and produces a 128-bit result (ending in l).

    // VABA	I	-	Absolute Difference and Accumulate
    check_neon("vaba.s8", 8, i8_1 + absd(i8_2, i8_3));
    check_neon("vaba.u8", 8, u8_1 + absd(u8_2, u8_3));
    check_neon("vaba.s16", 4, i16_1 + absd(i16_2, i16_3));
    check_neon("vaba.u16", 4, u16_1 + absd(u16_2, u16_3));
    check_neon("vaba.s32", 2, i32_1 + absd(i32_2, i32_3));
    check_neon("vaba.u32", 2, u32_1 + absd(u32_2, u32_3));
    check_neon("vaba.s8", 16, i8_1 + absd(i8_2, i8_3));
    check_neon("vaba.u8", 16, u8_1 + absd(u8_2, u8_3));
    check_neon("vaba.s16", 8, i16_1 + absd(i16_2, i16_3));
    check_neon("vaba.u16", 8, u16_1 + absd(u16_2, u16_3));
    check_neon("vaba.s32", 4, i32_1 + absd(i32_2, i32_3));
    check_neon("vaba.u32", 4, u32_1 + absd(u32_2, u32_3));

    // VABAL	I	-	Absolute Difference and Accumulate Long
    check_neon("vabal.s8", 8, i16_1 + absd(i16(i8_2), i16(i8_3)));
    check_neon("vabal.u8", 8, u16_1 + absd(u16(u8_2), u16(u8_3)));
    check_neon("vabal.s16", 4, i32_1 + absd(i32(i16_2), i32(i16_3)));
    check_neon("vabal.u16", 4, u32_1 + absd(u32(u16_2), u32(u16_3)));
    check_neon("vabal.s32", 2, i64_1 + absd(i64(i32_2), i64(i32_3)));
    check_neon("vabal.u32", 2, u64_1 + absd(u64(u32_2), u64(u32_3)));

    // VABD	I, F	-	Absolute Difference
    check_neon("vabd.s8", 8, absd(i8_2, i8_3));
    check_neon("vabd.u8", 8, absd(u8_2, u8_3));
    check_neon("vabd.s16", 4, absd(i16_2, i16_3));
    check_neon("vabd.u16", 4, absd(u16_2, u16_3));
    check_neon("vabd.s32", 2, absd(i32_2, i32_3));
    check_neon("vabd.u32", 2, absd(u32_2, u32_3));
    check_neon("vabd.s8", 16, absd(i8_2, i8_3));
    check_neon("vabd.u8", 16, absd(u8_2, u8_3));
    check_neon("vabd.s16", 8, absd(i16_2, i16_3));
    check_neon("vabd.u16", 8, absd(u16_2, u16_3));
    check_neon("vabd.s32", 4, absd(i32_2, i32_3));
    check_neon("vabd.u32", 4, absd(u32_2, u32_3));

    // VABDL	I	-	Absolute Difference Long    
    check_neon("vabdl.s8", 8, absd(i16(i8_2), i16(i8_3)));
    check_neon("vabdl.u8", 8, absd(u16(u8_2), u16(u8_3)));
    check_neon("vabdl.s16", 4, absd(i32(i16_2), i32(i16_3)));
    check_neon("vabdl.u16", 4, absd(u32(u16_2), u32(u16_3)));
    check_neon("vabdl.s32", 2, absd(i64(i32_2), i64(i32_3)));
    check_neon("vabdl.u32", 2, absd(u64(u32_2), u64(u32_3)));

    // VABS	I, F	F, D	Absolute
    check_neon("vabs.f32", 2, abs(f32_1));
    check_neon("vabs.s32", 2, abs(i32_1));
    check_neon("vabs.s16", 4, abs(i16_1));
    check_neon("vabs.s8", 8, abs(i8_1));
    check_neon("vabs.f32", 4, abs(f32_1));
    check_neon("vabs.s32", 4, abs(i32_1));
    check_neon("vabs.s16", 8, abs(i16_1));
    check_neon("vabs.s8", 16, abs(i8_1));    

    // VACGE	F	-	Absolute Compare Greater Than or Equal
    // VACGT	F	-	Absolute Compare Greater Than
    // VACLE	F	-	Absolute Compare Less Than or Equal
    // VACLT	F	-	Absolute Compare Less Than
    check_neon("vacge.f32", 2, select(abs(f32_1) >= abs(f32_2), 1.0f, 2.0f));
    check_neon("vacge.f32", 4, select(abs(f32_1) >= abs(f32_2), 1.0f, 2.0f));
    check_neon("vacgt.f32", 2, select(abs(f32_1) > abs(f32_2), 1.0f, 2.0f));
    check_neon("vacgt.f32", 4, select(abs(f32_1) > abs(f32_2), 1.0f, 2.0f));

    // VADD	I, F	F, D	Add
    check_neon("vadd.i8", 16, i8_1 + i8_2);
    check_neon("vadd.i8", 16, u8_1 + u8_2);
    check_neon("vadd.i16", 8, i16_1 + i16_2);
    check_neon("vadd.i16", 8, u16_1 + u16_2);
    check_neon("vadd.i32", 4, i32_1 + i32_2);
    check_neon("vadd.i32", 4, u32_1 + u32_2);
    check_neon("vadd.i64", 2, i64_1 + i64_2);
    check_neon("vadd.i64", 2, u64_1 + u64_2);
    check_neon("vadd.f32", 4, f32_1 + f32_2);
    check_neon("vadd.i8", 8, i8_1 + i8_2);
    check_neon("vadd.i8", 8, u8_1 + u8_2);
    check_neon("vadd.i16", 4, i16_1 + i16_2);
    check_neon("vadd.i16", 4, u16_1 + u16_2);
    check_neon("vadd.i32", 2, i32_1 + i32_2);
    check_neon("vadd.i32", 2, u32_1 + u32_2);
    check_neon("vadd.f32", 2, f32_1 + f32_2);

    // VADDHN	I	-	Add and Narrow Returning High Half
    check_neon("vaddhn.i16", 8, i8((i16_1 + i16_2)/256));
    check_neon("vaddhn.i16", 8, u8((u16_1 + u16_2)/256));
    check_neon("vaddhn.i32", 4, i16((i32_1 + i32_2)/65536));
    check_neon("vaddhn.i32", 4, u16((u32_1 + u32_2)/65536));
    check_neon("vaddhn.i16", 4, i8((i16_1 + i16_2)/256));
    check_neon("vaddhn.i16", 4, u8((u16_1 + u16_2)/256));
    check_neon("vaddhn.i32", 2, i16((i32_1 + i32_2)/65536));
    check_neon("vaddhn.i32", 2, u16((u32_1 + u32_2)/65536));
    // can't do 64-bit version because we don't pass through 64-bit int constants

    // VADDL	I	-	Add Long
    check_neon("vaddl.s8", 8, i16(i8_1) + i16(i8_2));
    check_neon("vaddl.u8", 8, u16(u8_1) + u16(u8_2));
    check_neon("vaddl.s16", 4, i32(i16_1) + i32(i16_2));
    check_neon("vaddl.u16", 4, u32(u16_1) + u32(u16_2));
    check_neon("vaddl.s32", 2, i64(i32_1) + i64(i32_2));
    check_neon("vaddl.u32", 2, u64(u32_1) + u64(u32_2));
    check_neon("vaddl.s8", 4, i16(i8_1) + i16(i8_2));
    check_neon("vaddl.u8", 4, u16(u8_1) + u16(u8_2));
    check_neon("vaddl.s16", 2, i32(i16_1) + i32(i16_2));
    check_neon("vaddl.u16", 2, u32(u16_1) + u32(u16_2));

    // VADDW	I	-	Add Wide
    check_neon("vaddw.s8", 8, i8_1 + i16_1);
    check_neon("vaddw.u8", 8, u8_1 + u16_1);
    check_neon("vaddw.s16", 4, i16_1 + i32_1);
    check_neon("vaddw.u16", 4, u16_1 + u32_1);
    check_neon("vaddw.s32", 2, i32_1 + i64_1);
    check_neon("vaddw.u32", 2, u32_1 + u64_1);
    check_neon("vaddw.s8", 4, i8_1 + i16_1);
    check_neon("vaddw.u8", 4, u8_1 + u16_1);
    check_neon("vaddw.s16", 2, i16_1 + i32_1);
    check_neon("vaddw.u16", 2, u16_1 + u32_1);

    // VAND	X	-	Bitwise AND
    // Not implemented in front-end yet
    // check_neon("vand", 4, bool1 & bool2);
    // check_neon("vand", 2, bool1 & bool2);

    // VBIC	I	-	Bitwise Clear
    // VBIF	X	-	Bitwise Insert if False
    // VBIT	X	-	Bitwise Insert if True
    // skip these ones

    // VBSL	X	-	Bitwise Select
    check_neon("vbsl", 4, select(f32_1 > f32_2, 1.0f, 2.0f));
    check_neon("vbsl", 2, select(f32_1 > f32_2, 1.0f, 2.0f));

    // VCEQ	I, F	-	Compare Equal
    check_neon("vceq.i8", 16, select(i8_1 == i8_2, i8(1), i8(2)));
    check_neon("vceq.i8", 16, select(u8_1 == u8_2, u8(1), u8(2)));
    check_neon("vceq.i16", 8, select(i16_1 == i16_2, i16(1), i16(2)));
    check_neon("vceq.i16", 8, select(u16_1 == u16_2, u16(1), u16(2)));
    check_neon("vceq.i32", 4, select(i32_1 == i32_2, i32(1), i32(2)));
    check_neon("vceq.i32", 4, select(u32_1 == u32_2, u32(1), u32(2)));
    check_neon("vceq.f32", 4, select(f32_1 == f32_2, 1.0f, 2.0f));
    check_neon("vceq.i8", 8, select(i8_1 == i8_2, i8(1), i8(2)));
    check_neon("vceq.i8", 8, select(u8_1 == u8_2, u8(1), u8(2)));
    check_neon("vceq.i16", 4, select(i16_1 == i16_2, i16(1), i16(2)));
    check_neon("vceq.i16", 4, select(u16_1 == u16_2, u16(1), u16(2)));
    check_neon("vceq.i32", 2, select(i32_1 == i32_2, i32(1), i32(2)));
    check_neon("vceq.i32", 2, select(u32_1 == u32_2, u32(1), u32(2)));
    check_neon("vceq.f32", 2, select(f32_1 == f32_2, 1.0f, 2.0f));


    // VCGE	I, F	-	Compare Greater Than or Equal
    // VCLE	I, F	-	Compare Less Than or Equal
    

    // VCLS	I	-	Count Leading Sign Bits
    // VCLT	I, F	-	Compare Less Than
    // VCLZ	I	-	Count Leading Zeros
    // VCMP	-	F, D	Compare Setting Flags
    // VCNT	I	-	Count Number of Set Bits
    // VCVT	I, F, H	I, F, D, H	Convert Between Floating-Point and 32-bit Integer Types
    // VDIV	-	F, D	Divide
    // VDUP	X	-	Duplicate
    // VEOR	X	-	Bitwise Exclusive OR
    // VEXT	I	-	Extract Elements and Concatenate
    // VHADD	I	-	Halving Add
    // VHSUB	I	-	Halving Subtract
    // VLD1	X	-	Load Single-Element Structures
    // VLD2	X	-	Load Two-Element Structures
    // VLD3	X	-	Load Three-Element Structures
    // VLD4	X	-	Load Four-Element Structures
    // VLDM	X	F, D	Load Multiple Registers
    // VLDR	X	F, D	Load Single Register
    // VMAX	I, F	-	Maximum
    // VMIN	I, F	-	Minimum
    // VMLA	I, F	F, D	Multiply Accumulate
    // VMLS	I, F	F, D	Multiply Subtract
    // VMLAL	I	-	Multiply Accumulate Long
    // VMLSL	I	-	Multiply Subtract Long
    // VMOV	X	F, D	Move Register or Immediate
    // VMOVL	I	-	Move Long
    // VMOVN	I	-	Move and Narrow
    // VMRS	X	F, D	Move Advanced SIMD or VFP Register to ARM Compute Engine
    // VMSR	X	F, D	Move ARM Core Register to Advanced SIMD or VFP
    // VMUL	I, F, P	F, D	Multiply
    // VMULL	I, F, P	-	Multiply Long
    // VMVN	X	-	Bitwise NOT
    // VNEG	I, F	F, D	Negate
    // VNMLA	-	F, D	Negative Multiply Accumulate
    // VNMLS	-	F, D	Negative Multiply Subtract
    // VNMUL	-	F, D	Negative Multiply
    // VORN	X	-	Bitwise OR NOT
    // VORR	X	-	Bitwise OR
    // VPADAL	I	-	Pairwise Add and Accumulate Long
    // VPADD	I, F	-	Pairwise Add
    // VPADDL	I	-	Pairwise Add Long
    // VPMAX	I, F	-	Pairwise Maximum
    // VPMIN	I, F	-	Pairwise Minimum
    // VPOP	X	F, D	Pop from Stack
    // VPUSH	X	F, D	Push to Stack
    // VQABS	I	-	Saturating Absolute
    // VQADD	I	-	Saturating Add
    // VQDMLAL	I	-	Saturating Double Multiply Accumulate Long
    // VQDMLSL	I	-	Saturating Double Multiply Subtract Long
    // VQDMULH	I	-	Saturating Doubling Multiply Returning High Half
    // VQDMULL	I	-	Saturating Doubling Multiply Long
    // VQMOVN	I	-	Saturating Move and Narrow
    // VQMOVUN	I	-	Saturating Move and Unsigned Narrow
    // VQNEG	I	-	Saturating Negate
    // VQRDMULH	I	-	Saturating Rounding Doubling Multiply Returning High Half
    // VQRSHL	I	-	Saturating Rounding Shift Left
    // VQRSHRN	I	-	Saturating Rounding Shift Right Narrow
    // VQRSHRUN	I	-	Saturating Rounding Shift Right Unsigned Narrow
    // VQSHL	I	-	Saturating Shift Left
    // VQSHLU	I	-	Saturating Shift Left Unsigned
    // VQSHRN	I	-	Saturating Shift Right Narrow
    // VQSHRUN	I	-	Saturating Shift Right Unsigned Narrow
    // VQSUB	I	-	Saturating Subtract
    // VRADDHN	I	-	Rounding Add and Narrow Returning High Half
    // VRECPE	I, F	-	Reciprocal Estimate
    // VRECPS	F	-	Reciprocal Step
    // VREV16	X	-	Reverse in Halfwords
    // VREV32	X	-	Reverse in Words
    // VREV64	X	-	Reverse in Doublewords
    // VRHADD	I	-	Rounding Halving Add
    // VRSHL	I	-	Rounding Shift Left
    // VRSHR	I	-	Rounding Shift Right
    // VRSHRN	I	-	Rounding Shift Right Narrow
    // VRSQRTE	I, F	-	Reciprocal Square Root Estimate
    // VRSQRTS	F	-	Reciprocal Square Root Step
    // VRSRA	I	-	Rounding Shift Right and Accumulate
    // VRSUBHN	I	-	Rounding Subtract and Narrow Returning High Half
    // VSHL	I	-	Shift Left
    // VSHLL	I	-	Shift Left Long
    // VSHR	I	-	Shift Right
    // VSHRN	I	-	Shift Right Narrow
    // VSLI	X	-	Shift Left and Insert
    // VSQRT	-	F, D	Square Root
    // VSRA	I	-	Shift Right and Accumulate
    // VSRI	X	-	Shift Right and Insert
    // VST1	X	-	Store single-element structures
    // VST2	X	-	Store two-element structures
    // VST3	X	-	Store three-element structures
    // VST4	X	-	Store four-element structures
    // VSTM	X	F, D	Store Multiple Registers
    // VSTR	X	F, D	Store Register
    // VSUB	I, F	F, D	Subtract
    // VSUBHN	I	-	Subtract and Narrow
    // VSUBL	I	-	Subtract Long
    // VSUBW	I	-	Subtract Wide
    // VSWP	I	-	Swap Contents
    // VTBL	X	-	Table Lookup
    // VTBX	X	-	Table Extension
    // VTRN	X	-	Transpose
    // VTST	I	-	Test Bits
    // VUZP	X	-	Unzip
    // VZIP	X	-	Zip

}

int main(int argc, char **argv) {

    char *target = getenv("HL_TARGET");
    if (!target || strcasecmp(target, "x86_64") == 0) {
	check_sse_all();
    } else {
	check_neon_all();
    }

    return failed ? -1 : 0;
}
