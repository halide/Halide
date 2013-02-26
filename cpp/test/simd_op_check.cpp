#include <Halide.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

using namespace Halide;

// This tests that we can correctly generate all the simd ops

using std::vector;

bool failed = false;
Var x, y;

bool use_avx, use_avx2;

char *filter = NULL;

struct job {
    const char *op;
    const char *args;
    const char *module;
    Func f;
    char *result;
};

vector<job> jobs;

void check(const char *op, int vector_width, Expr e, const char *args) {
    if (filter) {
	if (strncmp(op, filter, strlen(filter)) != 0) return;
    }
    std::string name = std::string("test_") + op + Internal::unique_name('_');
    for (int i = 0; i < name.size(); i++) {
        if (name[i] == '.') name[i] = '_';
    }
    Func f(name);
    f(x, y) = e;
    f.vectorize(x, vector_width);

    vector<Argument> arg_types;
    Argument arg("", true, Int(1));
    arg.name = "in_f32";
    arg_types.push_back(arg);
    arg.name = "in_f64";
    arg_types.push_back(arg);
    arg.name = "in_i8";
    arg_types.push_back(arg);
    arg.name = "in_u8";
    arg_types.push_back(arg);
    arg.name = "in_i16";
    arg_types.push_back(arg);
    arg.name = "in_u16";
    arg_types.push_back(arg);
    arg.name = "in_i32";
    arg_types.push_back(arg);
    arg.name = "in_u32";
    arg_types.push_back(arg);
    arg.name = "in_i64";
    arg_types.push_back(arg);
    arg.name = "in_u64";
    arg_types.push_back(arg);

    char *module = new char[1024];
    snprintf(module, 1024, "test_%s_%s", op, f.name().c_str());
    f.compile_to_assembly(module, arg_types);

    job j = {op, args, module, f, NULL};
    jobs.push_back(j);
}

void do_job(job &j) {
    const char *op = j.op;
    const char *args = j.args;
    const char *module = j.module;
    Func f = j.f;
    
    char cmd[1024];
    snprintf(cmd, 1024, 
	     "sed -n '/v._loop/,/v._after_loop/p' < %s | "
             "sed 's/@.*//' > %s.s && "
	     "grep \"\tv\\{0,1\\}%s\" %s.s > /dev/null", 
	     module, module, op, module);

    if (system(cmd) != 0) {
	j.result = new char[4096];
	snprintf(j.result, 1024, "%s did not generate. Instead we got:\n", op);
	char asmfile[1024];
	snprintf(asmfile, 1024, "%s.s", module);
	FILE *f = fopen(asmfile, "r");
	const int max_size = 1024;
	char *buf = j.result + strlen(j.result);
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
	fclose(f);
	failed = 1;
    } else {
    }
}

const int nThreads = 16;

void *worker(void *arg) {
    int n = *((int *)arg);
    for (size_t i = n; i < jobs.size(); i += nThreads) {
	do_job(jobs[i]);
    }
}

void do_all_jobs() {
    pthread_t threads[nThreads];
    int indices[nThreads];
    for (int i = 0; i < nThreads; i++) {
	indices[i] = i;
	pthread_create(threads + i, NULL, worker, indices + i);
    }
    for (int i = 0; i < nThreads; i++) {
	pthread_join(threads[i], NULL);
    }
}

void print_results() {
    for (size_t i = 0; i < jobs.size(); i++) {
	if (jobs[i].result) 
	    fprintf(stderr, "%s\n", jobs[i].result);
    }    
    fprintf(stderr, "Successfully generated: ");
    for (size_t i = 0; i < jobs.size(); i++) {
	if (!jobs[i].result) 
	    fprintf(stderr, "%s ", jobs[i].op);
    }
    fprintf(stderr, "\n");
}

void check_sse(const char *op, int vector_width, Expr e) {
    if (use_avx2) {
        check(op, vector_width, e, "-O3 -mattr=+avx,+avx2");
    } else if (use_avx) {
        check(op, vector_width, e, "-O3 -mattr=+avx");
    } else {
        check(op, vector_width, e, "-O3 -mattr=-avx");
    }
}

void check_neon(const char *op, int vector_width, Expr e) {
    check(op, vector_width, e, "-O3 -mattr=+neon");
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
    // const int min_i32 = 0x80000000, max_i32 = 0x7fffffff;
    const int max_u8 = 255;
    const int max_u16 = 65535;

    // MMX (in 128-bits)
    check_sse("paddb", 16, u8_1 + u8_2);
    check_sse("psubb", 16, u8_1 - u8_2);
    check_sse("paddsb", 16, i8(clamp(i16(i8_1) + i16(i8_2), min_i8, max_i8)));
    check_sse("psubsb", 16, i8(clamp(i16(i8_1) - i16(i8_2), min_i8, max_i8)));
    check_sse("paddusb", 16, u8(min(u16(u8_1) + u16(u8_2), max_u8)));
    check_sse("psubusb", 16, u8(max(i16(u8_1) - i16(u8_2), 0)));
    check_sse("paddw", 8, u16_1 + u16_2);
    check_sse("psubw", 8, u16_1 - u16_2);
    check_sse("paddsw", 8, i16(clamp(i32(i16_1) + i32(i16_2), min_i16, max_i16)));
    check_sse("psubsw", 8, i16(clamp(i32(i16_1) - i32(i16_2), min_i16, max_i16)));
    check_sse("paddusw", 8, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
    check_sse("psubusw", 8, u16(max(i32(u16_1) - i32(u16_2), 0)));
    check_sse("paddd", 4, i32_1 + i32_2);
    check_sse("psubd", 4, i32_1 - i32_2);
    check_sse("pmulhw", 8, i16((i32(i16_1) * i32(i16_2)) / (256*256)));
    check_sse("pmulhw", 8, i16_1 / 15);
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
    check_sse("pavgb", 16, u8((u16(u8_1) + u16(u8_2) + 1)/2));
    check_sse("pavgw", 8, u16((u32(u16_1) + u32(u16_2) + 1)/2));
    check_sse("pmaxsw", 8, max(i16_1, i16_2));
    check_sse("pminsw", 8, min(i16_1, i16_2));
    check_sse("pmaxub", 16, max(u8_1, u8_2));
    check_sse("pminub", 16, min(u8_1, u8_2));
    check_sse("pmulhuw", 8, u16((u32(u16_1) * u32(u16_2))/(256*256)));
    check_sse("pmulhuw", 8, u16_1 / 15);

    /* Not implemented yet in the front-end
    check_sse("andnps", 4, bool1 & (!bool2));
    check_sse("andps", 4, bool1 & bool2);
    check_sse("orps", 4, bool1 | bool2);    
    check_sse("xorps", 4, bool1 ^ bool2);    
    */

    check_sse("cmpeqps", 4, select(f32_1 == f32_2, 1.0f, 2.0f));
    //check_sse("cmpneqps", 4, select(f32_1 != f32_2, 1.0f, 2.0f));
    //check_sse("cmpleps", 4, select(f32_1 <= f32_2, 1.0f, 2.0f));
    check_sse("cmpltps", 4, select(f32_1 < f32_2, 1.0f, 2.0f));

    // These ones are not necessary, because we just flip the args and use the above two
    //check_sse("cmpnleps", 4, select(f32_1 > f32_2, 1.0f, 2.0f));
    //check_sse("cmpnltps", 4, select(f32_1 >= f32_2, 1.0f, 2.0f));

    check_sse("shufps", 4, in_f32(2*x));
    if (!use_avx) check_sse("pshufd", 4, in_f32(100-x));

    // SSE 2

    check_sse("addpd", 2, f64_1 + f64_2);
    check_sse("subpd", 2, f64_1 - f64_2);
    check_sse("mulpd", 2, f64_1 * f64_2);
    check_sse("divpd", 2, f64_1 / f64_2);
    check_sse("sqrtpd", 2, sqrt(f64_2));
    check_sse("maxpd", 2, max(f64_1, f64_2));
    check_sse("minpd", 2, min(f64_1, f64_2));

    check_sse("cmpeqpd", 2, select(f64_1 == f64_2, 1.0f, 2.0f));
    //check_sse("cmpneqpd", 2, select(f64_1 != f64_2, 1.0f, 2.0f));
    //check_sse("cmplepd", 2, select(f64_1 <= f64_2, 1.0f, 2.0f));
    check_sse("cmpltpd", 2, select(f64_1 < f64_2, 1.0f, 2.0f));

    // llvm is pretty flaky about which ops get generated for casts. We don't intend to catch these for now, so skip them.
    //check_sse("cvttpd2dq", 4, i32(f64_1));
    //check_sse("cvtdq2pd", 4, f64(i32_1));
    //check_sse("cvttps2dq", 4, i32(f32_1));
    //check_sse("cvtdq2ps", 4, f32(i32_1));    
    //check_sse("cvtps2pd", 4, f64(f32_1));
    //check_sse("cvtpd2ps", 4, f32(f64_1));

    check_sse("paddq", 4, i64_1 + i64_2);
    check_sse("psubq", 4, i64_1 - i64_2);
    check_sse("pmuludq", 4, u64_1 * u64_2);

    check_sse("packssdw", 8, i16(clamp(i32_1, min_i16, max_i16)));
    check_sse("packsswb", 16, i8(clamp(i16_1, min_i8, max_i8)));
    check_sse("packuswb", 16, u8(clamp(i16_1, 0, max_u8)));

    // SSE 3

    // We don't do horizontal add/sub ops, so nothing new here
    
    // SSSE 3
    check_sse("pabsb", 16, abs(i8_1));
    check_sse("pabsw", 8, abs(i16_1));
    check_sse("pabsd", 4, abs(i32_1));

    // SSE 4.1

    // skip dot product and argmin 

    // llvm doesn't distinguish between signed and unsigned multiplies
    //check_sse("pmuldq", 4, i64(i32_1) * i64(i32_2));
    check_sse("pmuludq", 4, u64(u32_1) * u64(u32_2));
    check_sse("pmulld", 4, i32_1 * i32_2);

    check_sse("blendvps", 4, select(f32_1 > 0.7f, f32_1, f32_2));
    check_sse("blendvpd", 2, select(f64_1 > cast<double>(0.7f), f64_1, f64_2));
    check_sse("pblendvb", 16, select(u8_1 > 7, u8_1, u8_2));

    check_sse("pmaxsb", 16, max(i8_1, i8_2));
    check_sse("pminsb", 16, min(i8_1, i8_2));
    check_sse("pmaxuw", 8, max(u16_1, u16_2));
    check_sse("pminuw", 8, min(u16_1, u16_2));
    check_sse("pmaxud", 4, max(u32_1, u32_2));
    check_sse("pminud", 4, min(u32_1, u32_2));
    check_sse("pmaxsd", 4, max(i32_1, i32_2));
    check_sse("pminsd", 4, min(i32_1, i32_2));

    check_sse("roundps", 4, round(f32_1));
    check_sse("roundpd", 2, round(f64_1));
    check_sse("roundps", 4, floor(f32_1));
    check_sse("roundpd", 2, floor(f64_1));
    check_sse("roundps", 4, ceil(f32_1));
    check_sse("roundpd", 2, ceil(f64_1));

    check_sse("pcmpeqq", 2, select(i64_1 == i64_2, i64(1), i64(2)));
    check_sse("packusdw", 8, u16(clamp(i32_1, 0, max_u16)));

    // SSE 4.2
    
    check_sse("pcmpgtq", 2, select(i64_1 > i64_2, i64(1), i64(2)));

    // AVX
    if (use_avx) {
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
	check_sse("vblendvpd", 4, select(f64_1 > cast<double>(0.7f), f64_1, f64_2));
	        
	check_sse("vcvttps2dq", 8, i32(f32_1));
	check_sse("vcvtdq2ps", 8, f32(i32_1));    
	check_sse("vcvttpd2dq", 8, i32(f64_1));
	check_sse("vcvtdq2pd", 8, f64(i32_1));
	check_sse("vcvtps2pd", 8, f64(f32_1));
	check_sse("vcvtpd2ps", 8, f32(f64_1));

        check_sse("vperm", 4, in_f32(100-x));
    }

    // AVX 2

    if (use_avx2) {
	check_sse("vpaddb", 32, u8_1 + u8_2);
	check_sse("vpsubb", 32, u8_1 - u8_2);
	check_sse("vpaddsb", 32, i8(clamp(i16(i8_1) + i16(i8_2), min_i8, max_i8)));
	check_sse("vpsubsb", 32, i8(clamp(i16(i8_1) - i16(i8_2), min_i8, max_i8)));
	check_sse("vpaddusb", 32, u8(min(u16(u8_1) + u16(u8_2), max_u8)));
	check_sse("vpsubusb", 32, u8(min(u16(u8_1) - u16(u8_2), max_u8)));
	check_sse("vpaddw", 16, u16_1 + u16_2);
	check_sse("vpsubw", 16, u16_1 - u16_2);
	check_sse("vpaddsw", 16, i16(clamp(i32(i16_1) + i32(i16_2), min_i16, max_i16)));
	check_sse("vpsubsw", 16, i16(clamp(i32(i16_1) - i32(i16_2), min_i16, max_i16)));
	check_sse("vpaddusw", 16, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
	check_sse("vpsubusw", 16, u16(min(u32(u16_1) - u32(u16_2), max_u16)));
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
	
	check_sse("vpavgb", 32, u8((u16(u8_1) + u16(u8_2) + 1)/2));
	check_sse("vpavgw", 16, u16((u32(u16_1) + u32(u16_2) + 1)/2));
	check_sse("vpmaxsw", 16, max(i16_1, i16_2));
	check_sse("vpminsw", 16, min(i16_1, i16_2));
	check_sse("vpmaxub", 32, max(u8_1, u8_2));
	check_sse("vpminub", 32, min(u8_1, u8_2));
	check_sse("vpmulhuw", 16, i16((i32(i16_1) * i32(i16_2))/(256*256)));
	
	check_sse("vpaddq", 8, i64_1 + i64_2);
	check_sse("vpsubq", 8, i64_1 - i64_2);
	check_sse("vpmuludq", 8, u64_1 * u64_2);
	
	check_sse("vpackssdw", 16, i16(clamp(i32_1, min_i16, max_i16)));
	check_sse("vpacksswb", 32, i8(clamp(i16_1, min_i8, max_i8)));
	check_sse("vpackuswb", 32, u8(clamp(i16_1, 0, max_u8)));
	
	check_sse("vpabsb", 32, abs(i8_1));
	check_sse("vpabsw", 16, abs(i16_1));
	check_sse("vpabsd", 8, abs(i32_1));
	
        // llvm doesn't distinguish between signed and unsigned multiplies
        // check_sse("vpmuldq", 8, i64(i32_1) * i64(i32_2));
        check_sse("vpmuludq", 8, u64(u32_1) * u64(u32_2));
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
	check_sse("vpackusdw", 16, u16(clamp(i32_1, 0, max_u16)));
	check_sse("vpcmpgtq", 4, select(i64_1 > i64_2, i64(1), i64(2)));
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
    //const int min_i32 = 0x80000000, max_i32 = 0x7fffffff;
    const int max_u8 = 255;
    const int max_u16 = 65535;

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

    // We add a bogus first term to prevent the select from
    // simplifying the >= to a < with the 1 and 2 switched. The
    // pattern to use is just abs(f32_1) >= abs(f32_2).
    check_neon("vacge.f32", 2, select((f32_1 == f32_2) || (abs(f32_1) >= abs(f32_2)), 1.0f, 2.0f));
    check_neon("vacge.f32", 4, select((f32_1 == f32_2) || (abs(f32_1) >= abs(f32_2)), 1.0f, 2.0f));

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
    /* Halide flips these to less than instead 
    check_neon("vcge.s8", 16, select(i8_1 >= i8_2, i8(1), i8(2)));
    check_neon("vcge.u8", 16, select(u8_1 >= u8_2, u8(1), u8(2)));
    check_neon("vcge.s16", 8, select(i16_1 >= i16_2, i16(1), i16(2)));
    check_neon("vcge.u16", 8, select(u16_1 >= u16_2, u16(1), u16(2)));
    check_neon("vcge.s32", 4, select(i32_1 >= i32_2, i32(1), i32(2)));
    check_neon("vcge.u32", 4, select(u32_1 >= u32_2, u32(1), u32(2)));
    check_neon("vcge.f32", 4, select(f32_1 >= f32_2, 1.0f, 2.0f));
    check_neon("vcge.s8", 8, select(i8_1 >= i8_2, i8(1), i8(2)));
    check_neon("vcge.u8", 8, select(u8_1 >= u8_2, u8(1), u8(2)));
    check_neon("vcge.s16", 4, select(i16_1 >= i16_2, i16(1), i16(2)));
    check_neon("vcge.u16", 4, select(u16_1 >= u16_2, u16(1), u16(2)));
    check_neon("vcge.s32", 2, select(i32_1 >= i32_2, i32(1), i32(2)));
    check_neon("vcge.u32", 2, select(u32_1 >= u32_2, u32(1), u32(2)));
    check_neon("vcge.f32", 2, select(f32_1 >= f32_2, 1.0f, 2.0f));
    */

    // VCGT	I, F	-	Compare Greater Than
    check_neon("vcgt.s8", 16, select(i8_1 > i8_2, i8(1), i8(2)));
    check_neon("vcgt.u8", 16, select(u8_1 > u8_2, u8(1), u8(2)));
    check_neon("vcgt.s16", 8, select(i16_1 > i16_2, i16(1), i16(2)));
    check_neon("vcgt.u16", 8, select(u16_1 > u16_2, u16(1), u16(2)));
    check_neon("vcgt.s32", 4, select(i32_1 > i32_2, i32(1), i32(2)));
    check_neon("vcgt.u32", 4, select(u32_1 > u32_2, u32(1), u32(2)));
    check_neon("vcgt.f32", 4, select(f32_1 > f32_2, 1.0f, 2.0f));
    check_neon("vcgt.s8", 8, select(i8_1 > i8_2, i8(1), i8(2)));
    check_neon("vcgt.u8", 8, select(u8_1 > u8_2, u8(1), u8(2)));
    check_neon("vcgt.s16", 4, select(i16_1 > i16_2, i16(1), i16(2)));
    check_neon("vcgt.u16", 4, select(u16_1 > u16_2, u16(1), u16(2)));
    check_neon("vcgt.s32", 2, select(i32_1 > i32_2, i32(1), i32(2)));
    check_neon("vcgt.u32", 2, select(u32_1 > u32_2, u32(1), u32(2)));
    check_neon("vcgt.f32", 2, select(f32_1 > f32_2, 1.0f, 2.0f));   

    // VCLS	I	-	Count Leading Sign Bits
    // VCLZ	I	-	Count Leading Zeros
    // VCMP	-	F, D	Compare Setting Flags
    // VCNT	I	-	Count Number of Set Bits
    // We skip these ones

    // VCVT	I, F, H	I, F, D, H	Convert Between Floating-Point and 32-bit Integer Types
    check_neon("vcvt.f32.u32", 2, f32(u32_1));
    check_neon("vcvt.f32.s32", 2, f32(i32_1));
    check_neon("vcvt.f32.u32", 4, f32(u32_1));
    check_neon("vcvt.f32.s32", 4, f32(i32_1));
    check_neon("vcvt.u32.f32", 2, u32(f32_1));
    check_neon("vcvt.s32.f32", 2, i32(f32_1));
    check_neon("vcvt.u32.f32", 4, u32(f32_1));
    check_neon("vcvt.s32.f32", 4, i32(f32_1));
    // skip the fixed point conversions for now

    // VDIV	-	F, D	Divide
    // This doesn't actually get vectorized. Not sure cortex processors can do vectorized division.
    check_neon("vdiv.f32", 4, f32_1/f32_2);
    check_neon("vdiv.f32", 2, f32_1/f32_2);
    check_neon("vdiv.f64", 2, f64_1/f64_2);

    // VDUP	X	-	Duplicate
    check_neon("vdup.8", 16, i8(y));
    check_neon("vdup.8", 16, u8(y));
    check_neon("vdup.16", 8, i16(y));
    check_neon("vdup.16", 8, u16(y));
    check_neon("vdup.32", 8, i32(y));
    check_neon("vdup.32", 8, u32(y));
    check_neon("vdup.32", 8, f32(y));

    // VEOR	X	-	Bitwise Exclusive OR
    // check_neon("veor", 4, bool1 ^ bool2);

    // VEXT	I	-	Extract Elements and Concatenate
    // unaligned loads with known offsets should use vext
    check_neon("vext.8", 16, in_i8(x+1));
    check_neon("vext.16", 8, in_i16(x+1));
    check_neon("vext.32", 4, in_i32(x+1));

    // VHADD	I	-	Halving Add
    check_neon("vhadd.s8", 16, i8((i16(i8_1) + i16(i8_2))/2));
    check_neon("vhadd.u8", 16, u8((u16(u8_1) + u16(u8_2))/2));
    check_neon("vhadd.s16", 8, i16((i32(i16_1) + i32(i16_2))/2));
    check_neon("vhadd.u16", 8, u16((u32(u16_1) + u32(u16_2))/2));
    check_neon("vhadd.s32", 4, i32((i64(i32_1) + i64(i32_2))/2));
    check_neon("vhadd.u32", 4, u32((u64(u32_1) + u64(u32_2))/2));
    check_neon("vhadd.s8", 8, i8((i16(i8_1) + i16(i8_2))/2));
    check_neon("vhadd.u8", 8, u8((u16(u8_1) + u16(u8_2))/2));
    check_neon("vhadd.s16", 4, i16((i32(i16_1) + i32(i16_2))/2));
    check_neon("vhadd.u16", 4, u16((u32(u16_1) + u32(u16_2))/2));
    check_neon("vhadd.s32", 2, i32((i64(i32_1) + i64(i32_2))/2));
    check_neon("vhadd.u32", 2, u32((u64(u32_1) + u64(u32_2))/2));
    // This is common enough that we also allow a version that ignores overflow issues
    check_neon("vhadd.s8", 16, (i8_1 + i8_2)/i8(2));
    check_neon("vhadd.u8", 16, (u8_1 + u8_2)/2);
    check_neon("vhadd.s16", 8, (i16_1 + i16_2)/2);
    check_neon("vhadd.u16", 8, (u16_1 + u16_2)/2);
    check_neon("vhadd.s32", 4, (i32_1 + i32_2)/2);
    check_neon("vhadd.u32", 4, (u32_1 + u32_2)/2);
    check_neon("vhadd.s8", 8, (i8_1 + i8_2)/i8(2));
    check_neon("vhadd.u8", 8, (u8_1 + u8_2)/2);
    check_neon("vhadd.s16", 4, (i16_1 + i16_2)/2);
    check_neon("vhadd.u16", 4, (u16_1 + u16_2)/2);
    check_neon("vhadd.s32", 2, (i32_1 + i32_2)/2);
    check_neon("vhadd.u32", 2, (u32_1 + u32_2)/2);

    // VHSUB	I	-	Halving Subtract
    check_neon("vhsub.s8", 16, i8((i16(i8_1) - i16(i8_2))/2));
    check_neon("vhsub.u8", 16, u8((u16(u8_1) - u16(u8_2))/2));
    check_neon("vhsub.s16", 8, i16((i32(i16_1) - i32(i16_2))/2));
    check_neon("vhsub.u16", 8, u16((u32(u16_1) - u32(u16_2))/2));
    check_neon("vhsub.s32", 4, i32((i64(i32_1) - i64(i32_2))/2));
    check_neon("vhsub.u32", 4, u32((u64(u32_1) - u64(u32_2))/2));
    check_neon("vhsub.s8", 8, i8((i16(i8_1) - i16(i8_2))/2));
    check_neon("vhsub.u8", 8, u8((u16(u8_1) - u16(u8_2))/2));
    check_neon("vhsub.s16", 4, i16((i32(i16_1) - i32(i16_2))/2));
    check_neon("vhsub.u16", 4, u16((u32(u16_1) - u32(u16_2))/2));
    check_neon("vhsub.s32", 2, i32((i64(i32_1) - i64(i32_2))/2));
    check_neon("vhsub.u32", 2, u32((u64(u32_1) - u64(u32_2))/2));
    // This is common enough that we also allow a version that ignores overflow issues
    check_neon("vhsub.s8", 16, (i8_1 - i8_2)/i8(2));
    check_neon("vhsub.u8", 16, (u8_1 - u8_2)/2);
    check_neon("vhsub.s16", 8, (i16_1 - i16_2)/2);
    check_neon("vhsub.u16", 8, (u16_1 - u16_2)/2);
    check_neon("vhsub.s32", 4, (i32_1 - i32_2)/2);
    check_neon("vhsub.u32", 4, (u32_1 - u32_2)/2);
    check_neon("vhsub.s8", 8, (i8_1 - i8_2)/i8(2));
    check_neon("vhsub.u8", 8, (u8_1 - u8_2)/2);
    check_neon("vhsub.s16", 4, (i16_1 - i16_2)/2);
    check_neon("vhsub.u16", 4, (u16_1 - u16_2)/2);
    check_neon("vhsub.s32", 2, (i32_1 - i32_2)/2);
    check_neon("vhsub.u32", 2, (u32_1 - u32_2)/2);

    // VLD1	X	-	Load Single-Element Structures
    // dense loads with unknown alignments should use vld1 variants
    check_neon("vld1.8", 16, in_i8(y));
    check_neon("vld1.8", 16, in_u8(y));
    check_neon("vld1.16", 8, in_i16(y));
    check_neon("vld1.16", 8, in_u16(y));
    check_neon("vld1.32", 4, in_i32(y));
    check_neon("vld1.32", 4, in_u32(y));
    check_neon("vld1.32", 4, in_f32(y));
    check_neon("vld1.8",  8, in_i8(y));
    check_neon("vld1.8",  8, in_u8(y));
    check_neon("vld1.16", 4, in_i16(y));
    check_neon("vld1.16", 4, in_u16(y));
    check_neon("vld1.32", 2, in_i32(y));
    check_neon("vld1.32", 2, in_u32(y));
    check_neon("vld1.32", 2, in_f32(y));

    // VLD2	X	-	Load Two-Element Structures
    check_neon("vld2.8", 16, in_i8(x*2) + in_i8(x*2+1));
    check_neon("vld2.8", 16, in_u8(x*2) + in_u8(x*2+1));
    check_neon("vld2.16", 8, in_i16(x*2) + in_i16(x*2+1));
    check_neon("vld2.16", 8, in_u16(x*2) + in_u16(x*2+1));
    check_neon("vld2.32", 4, in_i32(x*2) + in_i32(x*2+1));
    check_neon("vld2.32", 4, in_u32(x*2) + in_u32(x*2+1));
    check_neon("vld2.32", 4, in_f32(x*2) + in_f32(x*2+1));
    check_neon("vld2.8",  8, in_i8(x*2) + in_i8(x*2+1));
    check_neon("vld2.8",  8, in_u8(x*2) + in_u8(x*2+1));
    check_neon("vld2.16", 4, in_i16(x*2) + in_i16(x*2+1));
    check_neon("vld2.16", 4, in_u16(x*2) + in_u16(x*2+1));
    

    // VLD3	X	-	Load Three-Element Structures
    check_neon("vld3.8", 16, in_i8(x*3+y));
    check_neon("vld3.8", 16, in_u8(x*3+y));
    check_neon("vld3.16", 8, in_i16(x*3+y));
    check_neon("vld3.16", 8, in_u16(x*3+y));
    check_neon("vld3.32", 4, in_i32(x*3+y));
    check_neon("vld3.32", 4, in_u32(x*3+y));
    check_neon("vld3.32", 4, in_f32(x*3+y));
    check_neon("vld3.8",  8, in_i8(x*3+y));
    check_neon("vld3.8",  8, in_u8(x*3+y));
    check_neon("vld3.16", 4, in_i16(x*3+y));
    check_neon("vld3.16", 4, in_u16(x*3+y));

    // VLD4	X	-	Load Four-Element Structures
    check_neon("vld4.8", 16, in_i8(x*4+y));
    check_neon("vld4.8", 16, in_u8(x*4+y));
    check_neon("vld4.16", 8, in_i16(x*4+y));
    check_neon("vld4.16", 8, in_u16(x*4+y));
    check_neon("vld4.32", 4, in_i32(x*4+y));
    check_neon("vld4.32", 4, in_u32(x*4+y));
    check_neon("vld4.32", 4, in_f32(x*4+y));
    check_neon("vld4.8",  8, in_i8(x*4+y));
    check_neon("vld4.8",  8, in_u8(x*4+y));
    check_neon("vld4.16", 4, in_i16(x*4+y));
    check_neon("vld4.16", 4, in_u16(x*4+y));

    // VLDM	X	F, D	Load Multiple Registers
    // dense aligned loads should trigger this
    check_neon("vldmia", 16, in_i8(x));
    check_neon("vldmia", 16, in_u8(x));
    check_neon("vldmia", 8, in_i16(x));
    check_neon("vldmia", 8, in_u16(x));
    check_neon("vldmia", 4, in_i32(x));
    check_neon("vldmia", 4, in_u32(x));
    check_neon("vldmia", 4, in_f32(x));

    // VLDR	X	F, D	Load Single Register
    check_neon("vldr", 8, in_i8(x));
    check_neon("vldr", 8, in_u8(x));
    check_neon("vldr", 4, in_i16(x));
    check_neon("vldr", 4, in_u16(x));

    // VMAX	I, F	-	Maximum
    check_neon("vmax.s8", 16, max(i8_1, i8_2));
    check_neon("vmax.u8", 16, max(u8_1, u8_2));
    check_neon("vmax.s16", 8, max(i16_1, i16_2));
    check_neon("vmax.u16", 8, max(u16_1, u16_2));
    check_neon("vmax.s32", 4, max(i32_1, i32_2));
    check_neon("vmax.u32", 4, max(u32_1, u32_2));
    check_neon("vmax.f32", 4, max(f32_1, f32_2));
    check_neon("vmax.s8", 8, max(i8_1, i8_2));
    check_neon("vmax.u8", 8, max(u8_1, u8_2));
    check_neon("vmax.s16", 4, max(i16_1, i16_2));
    check_neon("vmax.u16", 4, max(u16_1, u16_2));
    check_neon("vmax.s32", 2, max(i32_1, i32_2));
    check_neon("vmax.u32", 2, max(u32_1, u32_2));
    check_neon("vmax.f32", 2, max(f32_1, f32_2));

    // VMIN	I, F	-	Minimum
    check_neon("vmin.s8", 16, min(i8_1, i8_2));
    check_neon("vmin.u8", 16, min(u8_1, u8_2));
    check_neon("vmin.s16", 8, min(i16_1, i16_2));
    check_neon("vmin.u16", 8, min(u16_1, u16_2));
    check_neon("vmin.s32", 4, min(i32_1, i32_2));
    check_neon("vmin.u32", 4, min(u32_1, u32_2));
    check_neon("vmin.f32", 4, min(f32_1, f32_2));
    check_neon("vmin.s8", 8, min(i8_1, i8_2));
    check_neon("vmin.u8", 8, min(u8_1, u8_2));
    check_neon("vmin.s16", 4, min(i16_1, i16_2));
    check_neon("vmin.u16", 4, min(u16_1, u16_2));
    check_neon("vmin.s32", 2, min(i32_1, i32_2));
    check_neon("vmin.u32", 2, min(u32_1, u32_2));
    check_neon("vmin.f32", 2, min(f32_1, f32_2));

    // VMLA	I, F	F, D	Multiply Accumulate
    check_neon("vmla.i8", 16, i8_1 + i8_2*i8_3);
    check_neon("vmla.i8", 16, u8_1 + u8_2*u8_3);
    check_neon("vmla.i16", 8, i16_1 + i16_2*i16_3);
    check_neon("vmla.i16", 8, u16_1 + u16_2*u16_3);
    check_neon("vmla.i32", 4, i32_1 + i32_2*i32_3);
    check_neon("vmla.i32", 4, u32_1 + u32_2*u32_3);
    check_neon("vmla.f32", 4, f32_1 + f32_2*f32_3);
    check_neon("vmla.f64", 2, f64_1 + f64_2*f64_3);
    check_neon("vmla.i8",  8, i8_1 + i8_2*i8_3);
    check_neon("vmla.i8",  8, u8_1 + u8_2*u8_3);
    check_neon("vmla.i16", 4, i16_1 + i16_2*i16_3);
    check_neon("vmla.i16", 4, u16_1 + u16_2*u16_3);
    check_neon("vmla.i32", 2, i32_1 + i32_2*i32_3);
    check_neon("vmla.i32", 2, u32_1 + u32_2*u32_3);
    check_neon("vmla.f32", 2, f32_1 + f32_2*f32_3);

    // VMLS	I, F	F, D	Multiply Subtract
    check_neon("vmls.i8", 16, i8_1 - i8_2*i8_3);
    check_neon("vmls.i8", 16, u8_1 - u8_2*u8_3);
    check_neon("vmls.i16", 8, i16_1 - i16_2*i16_3);
    check_neon("vmls.i16", 8, u16_1 - u16_2*u16_3);
    check_neon("vmls.i32", 4, i32_1 - i32_2*i32_3);
    check_neon("vmls.i32", 4, u32_1 - u32_2*u32_3);
    check_neon("vmls.f32", 4, f32_1 - f32_2*f32_3);
    check_neon("vmls.f64", 2, f64_1 - f64_2*f64_3);
    check_neon("vmls.i8",  8, i8_1 - i8_2*i8_3);
    check_neon("vmls.i8",  8, u8_1 - u8_2*u8_3);
    check_neon("vmls.i16", 4, i16_1 - i16_2*i16_3);
    check_neon("vmls.i16", 4, u16_1 - u16_2*u16_3);
    check_neon("vmls.i32", 2, i32_1 - i32_2*i32_3);
    check_neon("vmls.i32", 2, u32_1 - u32_2*u32_3);
    check_neon("vmls.f32", 2, f32_1 - f32_2*f32_3);

    // VMLAL	I	-	Multiply Accumulate Long
    check_neon("vmlal.s8",  8, i16_1 + i8_2*i8_3);
    check_neon("vmlal.u8",  8, u16_1 + u8_2*u8_3);
    check_neon("vmlal.s16", 4, i32_1 + i16_2*i16_3);
    check_neon("vmlal.u16", 4, u32_1 + u16_2*u16_3);
    check_neon("vmlal.s32", 2, i64_1 + i32_2*i32_3);
    check_neon("vmlal.u32", 2, u64_1 + u32_2*u32_3);

    // VMLSL	I	-	Multiply Subtract Long
    check_neon("vmlsl.s8",  8, i16_1 - i8_2*i8_3);
    check_neon("vmlsl.u8",  8, u16_1 - u8_2*u8_3);
    check_neon("vmlsl.s16", 4, i32_1 - i16_2*i16_3);
    check_neon("vmlsl.u16", 4, u32_1 - u16_2*u16_3);
    check_neon("vmlsl.s32", 2, i64_1 - i32_2*i32_3);
    check_neon("vmlsl.u32", 2, u64_1 - u32_2*u32_3);

    // VMOV	X	F, D	Move Register or Immediate
    // This is for loading immediates, which we won't do in the inner loop anyway

    // VMOVL	I	-	Move Long
    check_neon("vmovl.s8", 8, i16(i8_1));
    check_neon("vmovl.u8", 8, u16(u8_1));
    check_neon("vmovl.u8", 8, i16(u8_1));
    check_neon("vmovl.s16", 4, i32(i16_1));
    check_neon("vmovl.u16", 4, u32(u16_1));
    check_neon("vmovl.u16", 4, i32(u16_1));
    check_neon("vmovl.s32", 2, i64(i32_1));
    check_neon("vmovl.u32", 2, u64(u32_1));
    check_neon("vmovl.u32", 2, i64(u32_1));   

    // VMOVN	I	-	Move and Narrow
    check_neon("vmovn.i16", 8, i8(i16_1));
    check_neon("vmovn.i16", 8, u8(u16_1));
    check_neon("vmovn.i32", 4, i16(i32_1));
    check_neon("vmovn.i32", 4, u16(u32_1));
    check_neon("vmovn.i64", 2, i32(i64_1));
    check_neon("vmovn.i64", 2, u32(u64_1));

    // VMRS	X	F, D	Move Advanced SIMD or VFP Register to ARM compute Engine
    // VMSR	X	F, D	Move ARM Core Register to Advanced SIMD or VFP
    // trust llvm to use this correctly

    // VMUL	I, F, P	F, D	Multiply
    check_neon("vmul.i8", 16, i8_2*i8_1);
    check_neon("vmul.i8", 16, u8_2*u8_1);
    check_neon("vmul.i16", 8, i16_2*i16_1);
    check_neon("vmul.i16", 8, u16_2*u16_1);
    check_neon("vmul.i32", 4, i32_2*i32_1);
    check_neon("vmul.i32", 4, u32_2*u32_1);
    check_neon("vmul.f32", 4, f32_2*f32_1);
    check_neon("vmul.f64", 2, f64_2*f64_1);
    check_neon("vmul.i8",  8, i8_2*i8_1);
    check_neon("vmul.i8",  8, u8_2*u8_1);
    check_neon("vmul.i16", 4, i16_2*i16_1);
    check_neon("vmul.i16", 4, u16_2*u16_1);
    check_neon("vmul.i32", 2, i32_2*i32_1);
    check_neon("vmul.i32", 2, u32_2*u32_1);
    check_neon("vmul.f32", 2, f32_2*f32_1);

    // VMULL	I, F, P	-	Multiply Long
    check_neon("vmull.s8",  8, i16(i8_1)*i16(i8_2));
    check_neon("vmull.u8",  8, u16(u8_1)*u16(u8_2));
    check_neon("vmull.s16", 4, i32(i16_1)*i32(i16_2));
    check_neon("vmull.u16", 4, u32(u16_1)*u32(u16_2));
    check_neon("vmull.s32", 2, i64(i32_1)*i64(i32_2));
    check_neon("vmull.u32", 2, u64(u32_1)*u64(u32_2));

    // integer division by a constant should use fixed point
    // multiplication, which is done by using a widening multiply
    // followed by a narrowing
    check_neon("vmull.s8",  8, i8_1/37);
    check_neon("vmull.u8",  8, u8_1/201);
    check_neon("vmull.s16", 4, i16_1/84);
    check_neon("vmull.u16", 4, u16_1/723);
    check_neon("vmull.s32", 2, i32_1/3214);
    check_neon("vmull.u32", 2, u32_1/45623);

    // VMVN	X	-	Bitwise NOT
    // check_neon("vmvn", ~bool1);

    // VNEG	I, F	F, D	Negate
    check_neon("vneg.s8", 16, -i8_1);
    check_neon("vneg.s16", 8, -i16_1);
    check_neon("vneg.s32", 4, -i32_1);
    check_neon("vneg.s8", 8, -i8_1);
    check_neon("vneg.s16", 4, -i16_1);
    check_neon("vneg.s32", 2, -i32_1);
    check_neon("vneg.f32", 4, -f32_1);
    check_neon("vneg.f64", 2, -f64_1);

    // VNMLA	-	F, D	Negative Multiply Accumulate   
    // VNMLS	-	F, D	Negative Multiply Subtract
    // VNMUL	-	F, D	Negative Multiply
    // really? These seem awfully special-purpose
    check_neon("vnmla.f32", 4, -(f32_1 + f32_2*f32_3));
    check_neon("vnmla.f64", 2, -(f64_1 + f64_2*f64_3));    
    check_neon("vnmls.f32", 4, -(f32_1 - f32_2*f32_3));
    check_neon("vnmls.f64", 2, -(f64_1 - f64_2*f64_3));    
    check_neon("vnmul.f32", 4, -(f32_1*f32_2));
    check_neon("vnmul.f64", 2, -(f64_1*f64_2));    

    // VORN	X	-	Bitwise OR NOT
    // check_neon("vorn", bool1 | (~bool2));

    // VORR	X	-	Bitwise OR
    // check_neon("vorr", bool1 | bool2);

    // VPADAL	I	-	Pairwise Add and Accumulate Long
    // VPADD	I, F	-	Pairwise Add
    // VPADDL	I	-	Pairwise Add Long
    // VPMAX	I, F	-	Pairwise Maximum
    // VPMIN	I, F	-	Pairwise Minimum
    // We don't do horizontal ops

    // VPOP	X	F, D	Pop from Stack
    // VPUSH	X	F, D	Push to Stack
    // Not used by us

    // VQABS	I	-	Saturating Absolute
    check_neon("vqabs.s8", 16, i8(min(abs(i16(i8_1)),   max_i8)));
    check_neon("vqabs.s16", 8, i16(min(abs(i32(i16_1)), max_i16)));
    //check_neon("vqabs.s32", 4, i32(min(abs(i64(i32_1)), max_i32)));
    check_neon("vqabs.s8",  8, i8(min(abs(i16(i8_1)),   max_i8)));
    check_neon("vqabs.s16", 4, i16(min(abs(i32(i16_1)), max_i16)));
    //check_neon("vqabs.s32", 2, i32(min(abs(i64(i32_1)), max_i32)));

    // VQADD	I	-	Saturating Add
    check_neon("vqadd.s8", 16,  i8(clamp(i16(i8_1)  + i16(i8_2),  min_i8,  max_i8)));
    check_neon("vqadd.s16", 8, i16(clamp(i32(i16_1) + i32(i16_2), min_i16, max_i16)));
    //check_neon("vqadd.s32", 8, i32(clamp(i64(i32_1) + i64(i32_2), min_i32, max_i32)));
    check_neon("vqadd.s8",  8,  i8(clamp(i16(i8_1)  + i16(i8_2),  min_i8,  max_i8)));
    check_neon("vqadd.s16", 4, i16(clamp(i32(i16_1) + i32(i16_2), min_i16, max_i16)));
    //check_neon("vqadd.s32", 4, i32(clamp(i64(i32_1) + i64(i32_2), min_i32, max_i32)));

    check_neon("vqadd.u8", 16,  u8(min(u16(u8_1)  + u16(u8_2),  max_u8)));
    check_neon("vqadd.u16", 8, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
    check_neon("vqadd.u8",  8,  u8(min(u16(u8_1)  + u16(u8_2),  max_u8)));
    check_neon("vqadd.u16", 4, u16(min(u32(u16_1) + u32(u16_2), max_u16)));

    // Can't do larger ones because we only have i32 constants

    // VQDMLAL	I	-	Saturating Double Multiply Accumulate Long    
    // VQDMLSL	I	-	Saturating Double Multiply Subtract Long
    // VQDMULH	I	-	Saturating Doubling Multiply Returning High Half
    // VQDMULL	I	-	Saturating Doubling Multiply Long
    // Not sure why I'd use these

    // VQMOVN	I	-	Saturating Move and Narrow
    check_neon("vqmovn.s16", 8,  i8(clamp(i16_1, min_i8,  max_i8)));
    check_neon("vqmovn.s32", 4, i16(clamp(i32_1, min_i16, max_i16)));
    //check_neon("vqmovn.s64", 2, i32(clamp(i64_1, min_i32, max_i32)));
    check_neon("vqmovn.u16", 8,  u8(min(u16_1, max_u8)));
    check_neon("vqmovn.u32", 4, u16(min(u32_1, max_u16)));
    // Can't do the 64-bit one because we only have signed 32-bit consts

    // VQMOVUN	I	-	Saturating Move and Unsigned Narrow
    check_neon("vqmovun.u16", 8, u8(clamp(i16_1, 0, max_u8)));
    check_neon("vqmovun.u32", 4, u16(clamp(i32_1, 0, max_u16)));
    // Can't do the 64-bit one

    // VQNEG	I	-	Saturating Negate
    check_neon("vqneg.s8", 16, -max(i8_1,  -max_i8));
    check_neon("vqneg.s16", 8, -max(i16_1, -max_i16));
    //check_neon("vqneg.s32", 4, -max(i32_1, -max_i32));
    check_neon("vqneg.s8",  8, -max(i8_1,  -max_i8));
    check_neon("vqneg.s16", 4, -max(i16_1, -max_i16));
    //check_neon("vqneg.s32", 2, -max(i32_1, -max_i32));

    // VQRDMULH	I	-	Saturating Rounding Doubling Multiply Returning High Half
    // VQRSHL	I	-	Saturating Rounding Shift Left    
    // VQRSHRN	I	-	Saturating Rounding Shift Right Narrow
    // VQRSHRUN	I	-	Saturating Rounding Shift Right Unsigned Narrow
    // We use the non-rounding form of these (at worst we do an extra add)

    // VQSHL	I	-	Saturating Shift Left
    check_neon("vqshl.s8", 16,  i8(clamp(i16(i8_1)*16,  min_i8,  max_i8)));
    check_neon("vqshl.s16", 8, i16(clamp(i32(i16_1)*16, min_i16, max_i16)));
    //check_neon("vqshl.s32", 4, i32(clamp(i64(i32_1)*16, min_i32, max_i32)));
    check_neon("vqshl.s8",  8,  i8(clamp(i16(i8_1)*16,  min_i8,  max_i8)));
    check_neon("vqshl.s16", 4, i16(clamp(i32(i16_1)*16, min_i16, max_i16)));
    //check_neon("vqshl.s32", 2, i32(clamp(i64(i32_1)*16, min_i32, max_i32)));
    // skip the versions that we don't have constants for

    // VQSHLU	I	-	Saturating Shift Left Unsigned
    check_neon("vqshlu.u8", 16,  u8(min(u16(u8_1 )*16, max_u8)));
    check_neon("vqshlu.u16", 8, u16(min(u32(u16_1)*16, max_u16)));
    check_neon("vqshlu.u8",  8,  u8(min(u16(u8_1 )*16, max_u8)));
    check_neon("vqshlu.u16", 4, u16(min(u32(u16_1)*16, max_u16)));

    // VQSHRN	I	-	Saturating Shift Right Narrow
    // VQSHRUN	I	-	Saturating Shift Right Unsigned Narrow
    check_neon("vqshrn.s16", 8,  i8(clamp(i16_1/16, min_i8,  max_i8)));
    check_neon("vqshrn.s32", 4, i16(clamp(i32_1/16, min_i16, max_i16)));
    //check_neon("vqshrn.s64", 2, i32(clamp(i64_1/16, min_i32, max_i32)));
    check_neon("vqshrun.u16", 8,  u8(min(u16_1/16, max_u8)));
    check_neon("vqshrun.u32", 4, u16(min(u32_1/16, max_u16)));

    // VQSUB	I	-	Saturating Subtract
    check_neon("vqsub.s8", 16,  i8(clamp(i16(i8_1)  - i16(i8_2),  min_i8,  max_i8)));
    check_neon("vqsub.s16", 8, i16(clamp(i32(i16_1) - i32(i16_2), min_i16, max_i16)));
    //check_neon("vqsub.s32", 8, i32(clamp(i64(i32_1) - i64(i32_2), min_i32, max_i32)));
    check_neon("vqsub.s8",  8,  i8(clamp(i16(i8_1)  - i16(i8_2),  min_i8,  max_i8)));
    check_neon("vqsub.s16", 4, i16(clamp(i32(i16_1) - i32(i16_2), min_i16, max_i16)));
    //check_neon("vqsub.s32", 4, i32(clamp(i64(i32_1) - i64(i32_2), min_i32, max_i32)));

    // N.B. Saturating subtracts are expressed by widening to a *signed* type
    check_neon("vqsub.u8", 16,  u8(clamp(i16(u8_1)  - i16(u8_2),  0, max_u8)));
    check_neon("vqsub.u16", 8, u16(clamp(i32(u16_1) - i32(u16_2), 0, max_u16)));
    check_neon("vqsub.u8",  8,  u8(clamp(i16(u8_1)  - i16(u8_2),  0, max_u8)));
    check_neon("vqsub.u16", 4, u16(clamp(i32(u16_1) - i32(u16_2), 0, max_u16)));    

    // VRADDHN	I	-	Rounding Add and Narrow Returning High Half
    /* No rounding ops
    check_neon("vraddhn.i16", 8, i8((i16_1 + i16_2 + 128)/256));
    check_neon("vraddhn.i16", 8, u8((u16_1 + u16_2 + 128)/256));
    check_neon("vraddhn.i32", 4, i16((i32_1 + i32_2 + 32768)/65536));
    check_neon("vraddhn.i32", 4, u16((u32_1 + u32_2 + 32768)/65536));
    */

    // VRECPE	I, F	-	Reciprocal Estimate
    check_neon("vrecpe.f32", 4, 1.0f/f32_1);
    check_neon("vrecpe.f32", 2, 1.0f/f32_1);

    // VRECPS	F	-	Reciprocal Step
    // This does one newton-rhapson iteration for finding the reciprocal. Skip it.

    // VREV16	X	-	Reverse in Halfwords
    // VREV32	X	-	Reverse in Words
    // VREV64	X	-	Reverse in Doublewords    
    // A reverse dense load should trigger vrev
    check_neon("vrev64.16", 4, in_i16(100-x));
    //check_neon("vrev64.16", 8, in_i16(100-x)); This doesn't work :(

    // These reverse within each halfword, word, and doubleword
    // respectively. We don't use them. Instead we use vtbl for vector
    // shuffles.

    // VRHADD	I	-	Rounding Halving Add
    check_neon("vrhadd.s8", 16,  i8((i16(i8_1 ) + i16(i8_2 ) + 1)/2));
    check_neon("vrhadd.u8", 16,  u8((u16(u8_1 ) + u16(u8_2 ) + 1)/2));
    check_neon("vrhadd.s16", 8, i16((i32(i16_1) + i32(i16_2) + 1)/2));
    check_neon("vrhadd.u16", 8, u16((u32(u16_1) + u32(u16_2) + 1)/2));
    check_neon("vrhadd.s32", 4, i32((i64(i32_1) + i64(i32_2) + 1)/2));
    check_neon("vrhadd.u32", 4, u32((u64(u32_1) + u64(u32_2) + 1)/2));
    check_neon("vrhadd.s8",  8,  i8((i16(i8_1 ) + i16(i8_2 ) + 1)/2));
    check_neon("vrhadd.u8",  8,  u8((u16(u8_1 ) + u16(u8_2 ) + 1)/2));
    check_neon("vrhadd.s16", 4, i16((i32(i16_1) + i32(i16_2) + 1)/2));
    check_neon("vrhadd.u16", 4, u16((u32(u16_1) + u32(u16_2) + 1)/2));
    check_neon("vrhadd.s32", 2, i32((i64(i32_1) + i64(i32_2) + 1)/2));
    check_neon("vrhadd.u32", 2, u32((u64(u32_1) + u64(u32_2) + 1)/2));

    // VRSHL	I	-	Rounding Shift Left
    // VRSHR	I	-	Rounding Shift Right
    // VRSHRN	I	-	Rounding Shift Right Narrow
    // We use the non-rounding forms of these

    // VRSQRTE	I, F	-	Reciprocal Square Root Estimate
    check_neon("vrsqrte.f32", 4, 1.0f/sqrt(f32_1));

    // VRSQRTS	F	-	Reciprocal Square Root Step
    // One newtown rhapson iteration of 1/sqrt(x). Skip it.

    // VRSRA	I	-	Rounding Shift Right and Accumulate    
    // VRSUBHN	I	-	Rounding Subtract and Narrow Returning High Half
    // Boo rounding ops

    // VSHL	I	-	Shift Left
    check_neon("vshl.i8", 16,  i8_1*16);
    check_neon("vshl.i16", 8, i16_1*16);
    check_neon("vshl.i32", 4, i32_1*16);
    check_neon("vshl.i64", 2, i64_1*16);
    check_neon("vshl.i8",  8,  i8_1*16);
    check_neon("vshl.i16", 4, i16_1*16);
    check_neon("vshl.i32", 2, i32_1*16);
    check_neon("vshl.i8", 16,  u8_1*16);
    check_neon("vshl.i16", 8, u16_1*16);
    check_neon("vshl.i32", 4, u32_1*16);
    check_neon("vshl.i64", 2, u64_1*16);
    check_neon("vshl.i8",  8,  u8_1*16);
    check_neon("vshl.i16", 4, u16_1*16);
    check_neon("vshl.i32", 2, u32_1*16);

    
    // VSHLL	I	-	Shift Left Long
    check_neon("vshll.s8",  8, i16(i8_1)*16);
    check_neon("vshll.s16", 4, i32(i16_1)*16);
    check_neon("vshll.s32", 2, i64(i32_1)*16);
    check_neon("vshll.u8",  8, u16(u8_1)*16);
    check_neon("vshll.u16", 4, u32(u16_1)*16);
    check_neon("vshll.u32", 2, u64(u32_1)*16);

    // VSHR	I	-	Shift Right
    check_neon("vshr.s8", 16,  i8_1/16);
    check_neon("vshr.s16", 8, i16_1/16);
    check_neon("vshr.s32", 4, i32_1/16);
    check_neon("vshr.s64", 2, i64_1/16);
    check_neon("vshr.s8",  8,  i8_1/16);
    check_neon("vshr.s16", 4, i16_1/16);
    check_neon("vshr.s32", 2, i32_1/16);
    check_neon("vshr.u8", 16,  u8_1/16);
    check_neon("vshr.u16", 8, u16_1/16);
    check_neon("vshr.u32", 4, u32_1/16);
    check_neon("vshr.u64", 2, u64_1/16);
    check_neon("vshr.u8",  8,  u8_1/16);
    check_neon("vshr.u16", 4, u16_1/16);
    check_neon("vshr.u32", 2, u32_1/16);    

    // VSHRN	I	-	Shift Right Narrow
    check_neon("vshrn.i16", 8,  i8(i16_1/256));
    check_neon("vshrn.i32", 4, i16(i32_1/65536));
    check_neon("vshrn.i16",  8,  u8(u16_1/256));
    check_neon("vshrn.i32",  4, u16(u32_1/65536));
    check_neon("vshrn.i16", 8,  i8(i16_1/16));
    check_neon("vshrn.i32", 4, i16(i32_1/16));
    check_neon("vshrn.i16",  8,  u8(u16_1/16));
    check_neon("vshrn.i32",  4, u16(u32_1/16));

    // VSLI	X	-	Shift Left and Insert
    // I guess this could be used for (x*256) | (y & 255)? We don't do bitwise ops on integers, so skip it.

    // VSQRT	-	F, D	Square Root
    check_neon("vsqrt.f32", 4, sqrt(f32_1));
    check_neon("vsqrt.f64", 2, sqrt(f64_1));

    // VSRA	I	-	Shift Right and Accumulate
    check_neon("vsra.s8", 16,  i8_2 + i8_1/16);
    check_neon("vsra.s16", 8, i16_2 + i16_1/16);
    check_neon("vsra.s32", 4, i32_2 + i32_1/16);
    check_neon("vsra.s64", 2, i64_2 + i64_1/16);
    check_neon("vsra.s8",  8,  i8_2 + i8_1/16);
    check_neon("vsra.s16", 4, i16_2 + i16_1/16);
    check_neon("vsra.s32", 2, i32_2 + i32_1/16);
    check_neon("vsra.u8", 16,  u8_2 + u8_1/16);
    check_neon("vsra.u16", 8, u16_2 + u16_1/16);
    check_neon("vsra.u32", 4, u32_2 + u32_1/16);
    check_neon("vsra.u64", 2, u64_2 + u64_1/16);
    check_neon("vsra.u8",  8,  u8_2 + u8_1/16);
    check_neon("vsra.u16", 4, u16_2 + u16_1/16);
    check_neon("vsra.u32", 2, u32_2 + u32_1/16);        

    // VSRI	X	-	Shift Right and Insert
    // See VSLI

    // VST1	X	-	Store single-element structures
    check_neon("vst1.8", 16, i8_1);

    // VST2	X	-	Store two-element structures
    for (int sign = 0; sign <= 1; sign++) {
        for (int width = 128; width <= 256; width *= 2) {
            for (int bits = 8; bits < 64; bits *= 2) {
                if (width <= bits*2) continue;
                Func tmp1, tmp2;
                tmp1(x) = cast(sign ? Int(bits) : UInt(bits), x);
                tmp1.compute_root();
                tmp2(x, y) = select(x%2 == 0, tmp1(x/2), tmp1(x/2 + 16));
                tmp2.compute_root().vectorize(x, width/bits);
                char *op = (char *)malloc(32);
                snprintf(op, 32, "vst2.%d", bits);
                check_neon(op, width/bits, tmp2(0, 0) + tmp2(0, 63));
            }
        }
    }

    // VST3	X	-	Store three-element structures
    // VST4	X	-	Store four-element structures
    // Not supported for now. We need a better syntax for interleaving to take advantage of these

    // VSTM	X	F, D	Store Multiple Registers
    // VSTR	X	F, D	Store Register
    // we trust llvm to use these

    // VSUB	I, F	F, D	Subtract
    check_neon("vsub.i8", 16,  i8_1 - i8_2);
    check_neon("vsub.i8", 16,  u8_1 - u8_2);
    check_neon("vsub.i16", 8, i16_1 - i16_2);
    check_neon("vsub.i16", 8, u16_1 - u16_2);
    check_neon("vsub.i32", 4, i32_1 - i32_2);
    check_neon("vsub.i32", 4, u32_1 - u32_2);
    check_neon("vsub.i64", 2, i64_1 - i64_2);
    check_neon("vsub.i64", 2, u64_1 - u64_2);
    check_neon("vsub.f32", 4, f32_1 - f32_2);
    check_neon("vsub.i8",  8,  i8_1 - i8_2);
    check_neon("vsub.i8",  8,  u8_1 - u8_2);
    check_neon("vsub.i16", 4, i16_1 - i16_2);
    check_neon("vsub.i16", 4, u16_1 - u16_2);
    check_neon("vsub.i32", 2, i32_1 - i32_2);
    check_neon("vsub.i32", 2, u32_1 - u32_2);
    check_neon("vsub.f32", 2, f32_1 - f32_2);    

    // VSUBHN	I	-	Subtract and Narrow
    check_neon("vsubhn.i16", 8,  i8((i16_1 - i16_2)/256));
    check_neon("vsubhn.i16", 8,  u8((u16_1 - u16_2)/256));
    check_neon("vsubhn.i32", 4, i16((i32_1 - i32_2)/65536));
    check_neon("vsubhn.i32", 4, u16((u32_1 - u32_2)/65536));

    // VSUBL	I	-	Subtract Long
    check_neon("vsubl.s8",  8, i16(i8_1)  - i16(i8_2));
    check_neon("vsubl.u8",  8, u16(u8_1)  - u16(u8_2));
    check_neon("vsubl.s16", 4, i32(i16_1) - i32(i16_2));
    check_neon("vsubl.u16", 4, u32(u16_1) - u32(u16_2));
    check_neon("vsubl.s32", 2, i64(i32_1) - i64(i32_2));
    check_neon("vsubl.u32", 2, u64(u32_1) - u64(u32_2));

    // VSUBW	I	-	Subtract Wide
    check_neon("vsubw.s8",  8, i16_1 - i8_1);
    check_neon("vsubw.u8",  8, u16_1 - u8_1);
    check_neon("vsubw.s16", 4, i32_1 - i16_1);
    check_neon("vsubw.u16", 4, u32_1 - u16_1);
    check_neon("vsubw.s32", 2, i64_1 - i32_1);
    check_neon("vsubw.u32", 2, u64_1 - u32_1);

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
    // check_neon("vtst.32", 4, (bool1 & bool2) != 0);

    // VUZP	X	-	Unzip
    // VZIP	X	-	Zip
    // Interleave or deinterleave two vectors. Given that we use
    // interleaving loads and stores, it's hard to hit this op with
    // halide.

}

int main(int argc, char **argv) {

    if (argc > 1) filter = argv[1];
    else filter = NULL;

    char *target = getenv("HL_TARGET");
    use_avx = target && strstr(target, "avx");
    use_avx2 = target && strstr(target, "avx2");
    if (!target || strncasecmp(target, "x86", 3) == 0) {
	check_sse_all();
    } else if (strncasecmp(target, "arm", 3) == 0) {
	check_neon_all();
    }

    do_all_jobs();

    print_results();

    return failed ? -1 : 0;
}
