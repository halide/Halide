#include "Halide.h"
#include <stdarg.h>
#include <string.h>

using namespace Halide;

// This tests that we can correctly generate all the simd ops

bool failed = false;
Var x;

void check_sse(const char *op, int vector_width, Expr e) {
    Func f;
    f(x) = e;
    f.vectorize(x, vector_width);
    char module[1024];
    snprintf(module, 1024, "test_%s_%s", op, f.name().c_str());
    f.compileToFile(module);
    const char *llc = "../../../llvm/Release+Asserts/bin/llc";
    char cmd[1024];
    snprintf(cmd, 1024, 
	     "%s -mattr=+avx2,+avx %s.bc -o - | sed -n '/%s.v0_loop/,/%s.v0_afterloop/p' | grep '\t' > %s.s && grep %s %s.s", 
	     llc, module, f.name().c_str(), f.name().c_str(), module, op, module);

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

Expr abs(Expr e) {
    return select(e > 0, e, -e);
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


    Expr f64_1 = in_f64(x), f64_2 = in_f64(x+16);
    Expr f32_1 = in_f32(x), f32_2 = in_f32(x+16);
    Expr i8_1  = in_i8(x),  i8_2  = in_i8(x+16);
    Expr u8_1  = in_u8(x),  u8_2  = in_u8(x+16);
    Expr i16_1 = in_i16(x), i16_2 = in_i16(x+16);
    Expr u16_1 = in_u16(x), u16_2 = in_u16(x+16);
    Expr i32_1 = in_i32(x), i32_2 = in_i32(x+16);
    Expr u32_1 = in_u32(x), u32_2 = in_u32(x+16);
    Expr i64_1 = in_i64(x), i64_2 = in_i64(x+16);
    Expr u64_1 = in_u64(x), u64_2 = in_u64(x+16);
    Expr bool1 = (f32_1 > 0.3f), bool2 = (f32_1 < -0.3f);

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

void check_neon() {
}

int main(int argc, char **argv) {

    check_sse_all();

    return failed ? -1 : 0;
}
