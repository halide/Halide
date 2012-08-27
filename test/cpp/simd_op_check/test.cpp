#include "Halide.h"
#include <stdarg.h>
#include <string.h>

using namespace Halide;

// This tests that we can correctly generate all the simd ops

bool failed = false;
Var x;

void check_sse_expr(const char *op, int vector_width, Expr e) {
    Func f;
    f(x) = e;
    f.vectorize(x, vector_width);
    char module[1024];
    snprintf(module, 1024, "test_%s_%s", op, f.name().c_str());
    f.compileToFile(module);
    const char *llc = "../../../llvm/Release+Asserts/bin/llc";
    char cmd[1024];
    snprintf(cmd, 1024, 
	     "%s %s.bc -o - | sed -n '/%s.v0_loop/,/%s.v0_afterloop/p' | grep '\t' > %s.s && grep %s %s.s", 
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

void check_sse() {
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
    check_sse_expr("paddb", 16, u8_1 + u8_2);
    check_sse_expr("psubb", 16, u8_1 - u8_2);
    check_sse_expr("paddsb", 16, i8(clamp(i16(i8_1) + i16(i8_2), -128, 127)));
    check_sse_expr("psubsb", 16, i8(clamp(i16(i8_1) - i16(i8_2), -128, 127)));
    check_sse_expr("paddusb", 16, u8(clamp(u16(u8_1) + u16(u8_2), 0, 255)));
    check_sse_expr("psubusb", 16, u8(clamp(u16(u8_1) - u16(u8_2), 0, 255)));
    check_sse_expr("paddw", 8, u16_1 + u16_2);
    check_sse_expr("psubw", 8, u16_1 - u16_2);
    check_sse_expr("paddsw", 8, i16(clamp(i32(i16_1) + i32(i16_2), -32768, 32767)));
    check_sse_expr("psubsw", 8, i16(clamp(i32(i16_1) - i32(i16_2), -32768, 32767)));
    check_sse_expr("paddusw", 8, u16(clamp(u32(u16_1) + u32(u16_2), 0, 65535)));
    check_sse_expr("psubusw", 8, u16(clamp(u32(u16_1) - u32(u16_2), 0, 65535)));
    check_sse_expr("paddd", 4, i32_1 + i32_2);
    check_sse_expr("psubd", 4, i32_1 - i32_2);
    check_sse_expr("pmulhw", 8, i16((i32(i16_1) * i32(i16_2)) / (256*256)));
    check_sse_expr("pmullw", 8, i16_1 * i16_2);

    check_sse_expr("pcmpeqb", 16, select(u8_1 == u8_2, u8(1), u8(2)));
    check_sse_expr("pcmpgtb", 16, select(u8_1 > u8_2, u8(1), u8(2)));
    check_sse_expr("pcmpeqw", 8, select(u16_1 == u16_2, u16(1), u16(2)));
    check_sse_expr("pcmpgtw", 8, select(u16_1 > u16_2, u16(1), u16(2)));
    check_sse_expr("pcmpeqd", 4, select(u32_1 == u32_2, u32(1), u32(2)));
    check_sse_expr("pcmpgtd", 4, select(u32_1 > u32_2, u32(1), u32(2)));
    

    // SSE 1
    check_sse_expr("addps", 4, f32_1 + f32_2);
    check_sse_expr("subps", 4, f32_1 - f32_2);
    check_sse_expr("mulps", 4, f32_1 * f32_2);
    check_sse_expr("divps", 4, f32_1 / f32_2);
    check_sse_expr("rcpps", 4, 1.0f / f32_2);
    check_sse_expr("sqrtps", 4, sqrt(f32_2));
    check_sse_expr("rsqrtps", 4, 1.0f / sqrt(f32_2));
    check_sse_expr("maxps", 4, max(f32_1, f32_2));
    check_sse_expr("minps", 4, min(f32_1, f32_2));
    check_sse_expr("pavgb", 16, (u8_1 + u8_2)/2);
    check_sse_expr("pavgw", 8, (i16_1 + i16_2)/2);
    check_sse_expr("pmaxsw", 8, max(i16_1, i16_2));
    check_sse_expr("pminsw", 8, min(i16_1, i16_2));
    check_sse_expr("pmaxub", 16, max(u8_1, u8_2));
    check_sse_expr("pminub", 16, min(u8_1, u8_2));
    check_sse_expr("pmulhuw", 8, i16((i32(i16_1) * i32(i16_2))/(256*256)));

    /* Not implemented yet in the front-end
    check_sse_expr("andnps", bool1 & (!bool2));
    check_sse_expr("andps", bool1 & bool2);
    check_sse_expr("orps", bool1 | bool2);    
    check_sse_expr("xorps", bool1 ^ bool2);    
    */

    check_sse_expr("cmpeqps", 4, select(f32_1 == f32_2, 1.0f, 2.0f));
    check_sse_expr("cmpneqps", 4, select(f32_1 != f32_2, 1.0f, 2.0f));
    check_sse_expr("cmpleps", 4, select(f32_1 <= f32_2, 1.0f, 2.0f));
    check_sse_expr("cmpltps", 4, select(f32_1 < f32_2, 1.0f, 2.0f));

    // These ones are not necessary, because we just flip the args and use the above two
    //check_sse_expr("cmpnleps", 4, select(f32_1 > f32_2, 1.0f, 2.0f));
    //check_sse_expr("cmpnltps", 4, select(f32_1 >= f32_2, 1.0f, 2.0f));

    check_sse_expr("shufps", 4, in_f32(100-x));
    check_sse_expr("shufps", 4, in_f32(2*x));

    // SSE 2

    check_sse_expr("addpd", 2, f64_1 + f64_2);
    check_sse_expr("subpd", 2, f64_1 - f64_2);
    check_sse_expr("mulpd", 2, f64_1 * f64_2);
    check_sse_expr("divpd", 2, f64_1 / f64_2);
    check_sse_expr("sqrtpd", 2, sqrt(f64_2));
    check_sse_expr("maxpd", 2, max(f64_1, f64_2));
    check_sse_expr("minpd", 2, min(f64_1, f64_2));

    check_sse_expr("cmpeqpd", 2, select(f64_1 == f64_2, 1.0f, 2.0f));
    check_sse_expr("cmpneqpd", 2, select(f64_1 != f64_2, 1.0f, 2.0f));
    check_sse_expr("cmplepd", 2, select(f64_1 <= f64_2, 1.0f, 2.0f));
    check_sse_expr("cmpltpd", 2, select(f64_1 < f64_2, 1.0f, 2.0f));

    check_sse_expr("cvttps2dq", 4, i32(f32_1));
    check_sse_expr("cvtdq2ps", 4, f32(i32_1));    
    check_sse_expr("cvttpd2dq", 4, i32(f64_1));
    check_sse_expr("cvtdq2pd", 4, f64(i32_1));
    check_sse_expr("cvtps2pd", 4, f64(f32_1));
    check_sse_expr("cvtpd2ps", 4, f32(f64_1));

    check_sse_expr("paddq", 4, i64_1 + i64_2);
    check_sse_expr("psubq", 4, i64_1 - i64_2);
    check_sse_expr("pmuludq", 4, u64_1 * u64_2);

    // SSE 3

    // We don't do horizontal add/sub ops, so nothing new here
    
    // SSSE 3
    check_sse_expr("pabsb", 16, abs(i8_1));
    check_sse_expr("pabsw", 8, abs(i16_1));
    check_sse_expr("pabsd", 4, abs(i32_1));

    // SSE 4.1

    check_sse_expr("pmuldq", 2, i64(i32_1) * i64(i32_2));
    check_sse_expr("pmulld", 4, i32_1 * i32_2);

    // skip dot product and argmin 
    check_sse_expr("blendvps", 4, select(f32_1 > 0.7f, f32_1, f32_2));
    check_sse_expr("blendvpd", 2, select(f64_1 > 0.7, f64_1, f64_2));
    check_sse_expr("pblendvb", 16, select(u8_1 > 7, u8_1, u8_2));

    check_sse_expr("pmaxsb", 16, max(i8_1, i8_2));
    check_sse_expr("pminsb", 16, min(i8_1, i8_2));
    check_sse_expr("pmaxuw", 8, max(u16_1, u16_2));
    check_sse_expr("pminuw", 8, min(u16_1, u16_2));
    check_sse_expr("pmaxud", 8, max(u32_1, u32_2));
    check_sse_expr("pminud", 8, min(u32_1, u32_2));
    check_sse_expr("pmaxsd", 4, max(i32_1, i32_2));
    check_sse_expr("pminsd", 4, min(i32_1, i32_2));

    check_sse_expr("roundps", 4, round(f32_1));
    check_sse_expr("roundpd", 2, round(f64_1));

    check_sse_expr("pcmpeqq", 2, select(i64_1 == i64_2, i64(1), i64(2)));
    check_sse_expr("packusdw", 8, u16(clamp(i32_1, 0, 65535)));

    // SSE 4.2
    
    check_sse_expr("pcmpgtq", 2, select(i64_1 > i64_2, i64(1), i64(2)));

    // AVX

    // AVX2
}

void check_neon() {
}

int main(int argc, char **argv) {

    check_sse();

    return failed ? -1 : 0;
}
