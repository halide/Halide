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
	char buf[4096];
	memset(buf, 0, 4096);
	fread(buf, 1, 4096, f);
	fwrite(buf, 1, 4096, stderr);	
	fclose(f);
	fprintf(stderr, "\n");
	failed = 1;
    } 
}

Expr i32(Expr e) {
    return cast(Int(32), e);
}

Expr i16(Expr e) {
    return cast(Int(16), e);
}

Expr f32(Expr e) {
    return cast(Float(32), e);
}

void check_sse() {
    UniformImage in_f(Float(32), 1);
    UniformImage in_u8(UInt(8), 1);
    UniformImage in_i16(Int(16), 1);
    UniformImage in_i32(Int(32), 1);

    Expr f32_1 = in_f(x), f32_2 = in_f(x+1);
    Expr u8_1 = in_u8(x), u8_2 = in_u8(x+1);
    Expr i16_1 = in_i16(x), i16_2 = in_i16(x+1);
    Expr i32_1 = in_i32(x), i32_2 = in_i32(x+1);
    Expr bool1 = (f32_1 > 0.3f), bool2 = (f32_1 < -0.3f);

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

    /* Not implemented yet
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

    check_sse_expr("shufps", 4, in_f(100-x));
    check_sse_expr("shufps", 4, in_f(2*x));

    // SSE 2


    check_sse_expr("cvttps2dq", 4, i32(f32_1));
    check_sse_expr("cvtdq2ps", 4, f32(i32_1));
    

}

void check_neon() {
}

int main(int argc, char **argv) {

    check_sse();

    return failed ? -1 : 0;
}
