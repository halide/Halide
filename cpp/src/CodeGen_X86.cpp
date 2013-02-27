#include "CodeGen_X86.h"
#include "IROperator.h"
#include <iostream>
#include "buffer_t.h"
#include "IRPrinter.h"
#include "IRMatch.h"
#include "Log.h"
#include "Util.h"
#include "Var.h"
#include "Param.h"
#include "integer_division_table.h"

#include <llvm/Config/config.h>

// Temporary affordance to compile with both llvm 3.2 and 3.3.
// Protected as at least one installation of llvm elides version macros.
#if defined(LLVM_VERSION_MINOR) && LLVM_VERSION_MINOR < 3
#include <llvm/Value.h>
#include <llvm/Module.h>
#include <llvm/Function.h>
#include <llvm/TargetTransformInfo.h>
#include <llvm/DataLayout.h>
#include <llvm/IRBuilder.h>
#else
#include <llvm/IR/Value.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>
#endif

#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/IRReader.h>

extern "C" unsigned char halide_internal_initmod_x86[];
extern "C" int halide_internal_initmod_x86_length;
extern "C" unsigned char halide_internal_initmod_x86_avx[];
extern "C" int halide_internal_initmod_x86_avx_length;

namespace Halide { 
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_X86::CodeGen_X86(bool sse_41, bool avx) : CodeGen_Posix(), use_sse_41(sse_41), use_avx(avx) {
    assert(llvm_X86_enabled && "llvm build not configured with X86 target enabled.");
}

void CodeGen_X86::compile(Stmt stmt, string name, const vector<Argument> &args) {

    if (module && owns_module) delete module;

    StringRef sb;

    if (use_avx) {
        assert(halide_internal_initmod_x86_avx_length && "initial module for x86_avx is empty");
        sb = StringRef((char *)halide_internal_initmod_x86_avx, halide_internal_initmod_x86_avx_length);
    } else {
        assert(halide_internal_initmod_x86_length && "initial module for x86 is empty");
        sb = StringRef((char *)halide_internal_initmod_x86, halide_internal_initmod_x86_length);
    }
    MemoryBuffer *bitcode_buffer = MemoryBuffer::getMemBuffer(sb);

    // Parse it    
    module = ParseBitcodeFile(bitcode_buffer, context);

    // Fix the target triple
    log(1) << "Target triple of initial module: " << module->getTargetTriple() << "\n";

    // For now we'll just leave it as whatever the module was
    // compiled as. This assumes that we're not cross-compiling
    // between different x86 operating systems
    // module->setTargetTriple( ... );
        
    // Pass to the generic codegen
    CodeGen::compile(stmt, name, args);
    delete bitcode_buffer;
}

Expr _i64(Expr e) {
    return cast(Int(64, e.type().width), e);
}

Expr _u64(Expr e) {
    return cast(UInt(64, e.type().width), e);
}
Expr _i32(Expr e) {
    return cast(Int(32, e.type().width), e);
}

Expr _u32(Expr e) {
    return cast(UInt(32, e.type().width), e);
}

Expr _i16(Expr e) {
    return cast(Int(16, e.type().width), e);
}

Expr _u16(Expr e) {
    return cast(UInt(16, e.type().width), e);
}
 
Expr _i8(Expr e) {
    return cast(Int(8, e.type().width), e);
}

Expr _u8(Expr e) {
    return cast(UInt(8, e.type().width), e);
}

Expr _f32(Expr e) {
    return cast(Float(32, e.type().width), e);
}

Expr _f64(Expr e) {
    return cast(Float(64, e.type().width), e);
}

Value *CodeGen_X86::call_intrin(Type result_type, const string &name, vector<Expr> args) {
    vector<Value *> arg_values(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        arg_values[i] = codegen(args[i]);
    }

    return call_intrin(result_type, name, arg_values);
}

Value *CodeGen_X86::call_intrin(Type result_type, const string &name, vector<Value *> arg_values) {
    vector<llvm::Type *> arg_types(arg_values.size());
    for (size_t i = 0; i < arg_values.size(); i++) {
        arg_types[i] = arg_values[i]->getType();
    }

    llvm::Function *fn = module->getFunction("llvm.x86." + name);

    if (!fn) {
        FunctionType *func_t = FunctionType::get(llvm_type_of(result_type), arg_types, false);    
        fn = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, "llvm.x86." + name, module);
        fn->setCallingConv(CallingConv::C);
    }

    return builder->CreateCall(fn, arg_values);
}
 
void CodeGen_X86::visit(const Cast *op) {

    vector<Expr> matches;
 
    struct Pattern {
        bool needs_sse_41;
        bool extern_call;
        Type type;
        string intrin;
        Expr pattern;
    };

    Pattern patterns[] = {
        {false, false, Int(8, 16), "sse2.padds.b", 
         _i8(clamp(_i16(wild_i8x16) + _i16(wild_i8x16), -128, 127))},
        {false, false, Int(8, 16), "sse2.psubs.b", 
         _i8(clamp(_i16(wild_i8x16) - _i16(wild_i8x16), -128, 127))},
        {false, false, UInt(8, 16), "sse2.paddus.b", 
         _u8(min(_u16(wild_u8x16) + _u16(wild_u8x16), 255))},
        {false, false, UInt(8, 16), "sse2.psubus.b", 
         _u8(max(_i16(wild_u8x16) - _i16(wild_u8x16), 0))},
        {false, false, Int(16, 8), "sse2.padds.w", 
         _i16(clamp(_i32(wild_i16x8) + _i32(wild_i16x8), -32768, 32767))},
        {false, false, Int(16, 8), "sse2.psubs.w", 
         _i16(clamp(_i32(wild_i16x8) - _i32(wild_i16x8), -32768, 32767))},
        {false, false, UInt(16, 8), "sse2.paddus.w", 
         _u16(min(_u32(wild_u16x8) + _u32(wild_u16x8), 65535))},
        {false, false, UInt(16, 8), "sse2.psubus.w", 
         _u16(max(_i32(wild_u16x8) - _i32(wild_u16x8), 0))},
        {false, false, Int(16, 8), "sse2.pmulh.w", 
         _i16((_i32(wild_i16x8) * _i32(wild_i16x8)) / 65536)},
        {false, false, UInt(16, 8), "sse2.pmulhu.w", 
         _u16((_u32(wild_u16x8) * _u32(wild_u16x8)) / 65536)},
        {false, false, UInt(8, 16), "sse2.pavg.b",
         _u8(((_u16(wild_u8x16) + _u16(wild_u8x16)) + 1) / 2)},
        {false, false, UInt(16, 8), "sse2.pavg.w",
         _u16(((_u32(wild_u16x8) + _u32(wild_u16x8)) + 1) / 2)},
        {false, true, Int(16, 8), "packssdw", 
         _i16(clamp(wild_i32x8, -32768, 32767))},
        {false, true, Int(8, 16), "packsswb", 
         _i8(clamp(wild_i16x16, -128, 127))},
        {false, true, UInt(8, 16), "packuswb", 
         _u8(clamp(wild_i16x16, 0, 255))},
        {true, true, UInt(16, 8), "packusdw",
         _u16(clamp(wild_i32x8, 0, 65535))}
    };
        
    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        const Pattern &pattern = patterns[i];
        if (!use_sse_41 && pattern.needs_sse_41) continue;
        if (expr_match(pattern.pattern, op, matches)) {
            if (pattern.extern_call) {
                value = codegen(new Call(pattern.type, pattern.intrin, matches));
            } else {
                value = call_intrin(pattern.type, pattern.intrin, matches);
            }
            return;
        }
    }
        
    CodeGen::visit(op);

    /*
    check_sse("paddsb", 16, i8(clamp(i16(i8_1) + i16(i8_2), min_i8, 127)));
    check_sse("psubsb", 16, i8(clamp(i16(i8_1) - i16(i8_2), min_i8, 127)));
    check_sse("paddusb", 16, u8(min(u16(u8_1) + u16(u8_2), max_u8)));
    check_sse("psubusb", 16, u8(min(u16(u8_1) - u16(u8_2), max_u8)));
    check_sse("paddsw", 8, i16(clamp(i32(i16_1) + i32(i16_2), -32768, 32767)));
    check_sse("psubsw", 8, i16(clamp(i32(i16_1) - i32(i16_2), -32768, 32767)));
    check_sse("paddusw", 8, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
    check_sse("psubusw", 8, u16(min(u32(u16_1) - u32(u16_2), max_u16)));
    check_sse("pmulhw", 8, i16((i32(i16_1) * i32(i16_2)) / (256*256)));
    check_sse("pmulhw", 8, i16_1 / 15);

    // SSE 1
    check_sse("rcpps", 4, 1.0f / f32_2);
    check_sse("rsqrtps", 4, 1.0f / sqrt(f32_2));
    check_sse("pavgb", 16, u8((u16(u8_1) + u16(u8_2) + 1)/2));
    check_sse("pavgw", 8, u16((u32(u16_1) + u32(u16_2) + 1)/2));

    check_sse("pmulhuw", 8, u16((u32(u16_1) * u32(u16_2))/(256*256)));
    check_sse("pmulhuw", 8, u16_1 / 15);

    check_sse("shufps", 4, in_f32(2*x));

    // SSE 2
    check_sse("packssdw", 8, i16(clamp(i32_1, -32768, 32767)));
    check_sse("packsswb", 16, i8(clamp(i16_1, min_i8, 127)));
    check_sse("packuswb", 16, u8(clamp(i16_1, 0, max_u8)));

    check_sse("packusdw", 8, u16(clamp(i32_1, 0, max_u16)));
    */
}

void CodeGen_X86::visit(const Div *op) {    

    // Detect if it's a small int division
    const Broadcast *broadcast = op->b.as<Broadcast>();
    const Cast *cast_b = broadcast ? broadcast->value.as<Cast>() : NULL;    
    const IntImm *int_imm = cast_b ? cast_b->value.as<IntImm>() : NULL;
    int const_divisor = int_imm ? int_imm->value : 0;

    vector<Expr> matches;    
    if (op->type == Float(32, 4) && is_one(op->a)) {
        // Reciprocal and reciprocal square root
        if (expr_match(new Call(Float(32, 4), "sqrt_f32", vec(wild_f32x4)), op->b, matches)) {            
            value = call_intrin(Float(32, 4), "sse.rsqrt.ps", matches);
        } else {
            value = call_intrin(Float(32, 4), "sse.rcp.ps", vec(op->b));
        }
    } else if (use_avx && op->type == Float(32, 8) && is_one(op->a)) {
        // Reciprocal and reciprocal square root
        if (expr_match(new Call(Float(32, 8), "sqrt_f32", vec(wild_f32x8)), op->b, matches)) {            
            value = call_intrin(Float(32, 8), "avx.rsqrt.ps.256", matches);
        } else {
            value = call_intrin(Float(32, 8), "avx.rcp.ps.256", vec(op->b));
        }
    } else if (op->type == Int(16, 8) && const_divisor > 1 && const_divisor < 64) {
        int method     = IntegerDivision::table_s16[const_divisor-2][0];
        int multiplier = IntegerDivision::table_s16[const_divisor-2][1];
        int shift      = IntegerDivision::table_s16[const_divisor-2][2];        

        Value *val = codegen(op->a);
        
        // Start with multiply and keep high half
        Value *mult;
        if (multiplier != 0) {
            mult = codegen(cast(op->type, multiplier));
            mult = call_intrin(Int(16, 8), "sse2.pmulh.w", vec(val, mult));

            // Possibly add a correcting factor
            if (method == 1) {
                mult = builder->CreateAdd(mult, val);
            }
        } else {
            mult = val;
        }

        // Do the shift
        Value *sh;
        if (shift) {
            sh = codegen(cast(op->type, shift));
            mult = builder->CreateAShr(mult, sh);
        }

        // Add one for negative numbers
        sh = codegen(cast(op->type, op->type.bits - 1));
        Value *sign_bit = builder->CreateLShr(val, sh);
        value = builder->CreateAdd(mult, sign_bit);
    } else if (op->type == UInt(16, 8) && const_divisor > 1 && const_divisor < 64) {
        int method     = IntegerDivision::table_u16[const_divisor-2][0];
        int multiplier = IntegerDivision::table_u16[const_divisor-2][1];
        int shift      = IntegerDivision::table_u16[const_divisor-2][2];        

        Value *val = codegen(op->a);
        
        // Start with multiply and keep high half
        Value *mult = val;
        if (method > 0) {
            mult = codegen(cast(op->type, multiplier));
            mult = call_intrin(UInt(16, 8), "sse2.pmulhu.w", vec(val, mult));

            // Possibly add a correcting factor
            if (method == 2) {
                Value *correction = builder->CreateSub(val, mult);
                correction = builder->CreateLShr(correction, codegen(make_one(op->type)));
                mult = builder->CreateAdd(mult, correction);
            }
        }

        // Do the shift
        Value *sh = codegen(cast(op->type, shift));
        value = builder->CreateLShr(mult, sh);

    } else {    
        CodeGen::visit(op);
    }
}

void CodeGen_X86::visit(const Min *op) {
    if (op->type == UInt(8, 16)) {
        value = call_intrin(UInt(8, 16), "sse2.pminu.b", vec(op->a, op->b));
    } else if (use_sse_41 && op->type == Int(8, 16)) {
        value = call_intrin(Int(8, 16), "sse41.pminsb", vec(op->a, op->b));
    } else if (op->type == Int(16, 8)) {
        value = call_intrin(Int(16, 8), "sse2.pmins.w", vec(op->a, op->b));
    } else if (use_sse_41 && op->type == UInt(16, 8)) {
        value = call_intrin(UInt(16, 8), "sse41.pminuw", vec(op->a, op->b));
    } else if (use_sse_41 && op->type == Int(32, 4)) {
        value = call_intrin(Int(32, 4), "sse41.pminsd", vec(op->a, op->b));
    } else if (use_sse_41 && op->type == UInt(32, 4)) {
        value = call_intrin(UInt(32, 4), "sse41.pminud", vec(op->a, op->b));               
    } else {
        CodeGen::visit(op);
    }
}

void CodeGen_X86::visit(const Max *op) {
    if (op->type == UInt(8, 16)) {
        value = call_intrin(UInt(8, 16), "sse2.pmaxu.b", vec(op->a, op->b));
    } else if (use_sse_41 && op->type == Int(8, 16)) {
        value = call_intrin(Int(8, 16), "sse41.pmaxsb", vec(op->a, op->b));
    } else if (op->type == Int(16, 8)) {
        value = call_intrin(Int(16, 8), "sse2.pmaxs.w", vec(op->a, op->b));
    } else if (use_sse_41 && op->type == UInt(16, 8)) {
        value = call_intrin(UInt(16, 8), "sse41.pmaxuw", vec(op->a, op->b));
    } else if (use_sse_41 && op->type == Int(32, 4)) {
        value = call_intrin(Int(32, 4), "sse41.pmaxsd", vec(op->a, op->b));
    } else if (use_sse_41 && op->type == UInt(32, 4)) {
        value = call_intrin(UInt(32, 4), "sse41.pmaxud", vec(op->a, op->b));               
    } else {
        CodeGen::visit(op);
    }    
}

static bool extern_function_1_was_called = false;
extern "C" int extern_function_1(float x) {
    extern_function_1_was_called = true;
    return x < 0.4 ? 3 : 1;
}

void CodeGen_X86::test() {
    // corner cases to test:
    // signed mod by power of two, non-power of two
    // loads of mismatched types (e.g. load a float from something allocated as an array of ints)
    // Calls to vectorized externs, and externs for which no vectorized version exists

    Argument buffer_arg("buf", true, Int(0));
    Argument float_arg("alpha", false, Float(32));
    Argument int_arg("beta", false, Int(32));
    vector<Argument> args(3);
    args[0] = buffer_arg;
    args[1] = float_arg;
    args[2] = int_arg;        
    Var x("x"), i("i");
    Param<float> alpha("alpha");
    Param<int> beta("beta");

    // We'll clear out the initial buffer except for the first and
    // last two elements using dense unaligned vectors
    Stmt init = new For("i", 0, 3, For::Serial, 
                        new Store("buf", 
                                  new Ramp(i*4+2, 1, 4),
                                  new Ramp(i*4+2, 1, 4)));

    // Now set the first two elements using scalars, and last four elements using a dense aligned vector
    init = new Block(init, new Store("buf", 0, 0));
    init = new Block(init, new Store("buf", 1, 1));
    init = new Block(init, new Store("buf", new Ramp(12, 1, 4), new Ramp(12, 1, 4)));

    // Then multiply the even terms by 17 using sparse vectors
    init = new Block(init, 
                     new For("i", 0, 2, For::Serial, 
                             new Store("buf", 
                                       new Mul(new Broadcast(17, 4), 
                                               new Load(Int(32, 4), "buf", new Ramp(i*8, 2, 4), Buffer(), Parameter())),
                                       new Ramp(i*8, 2, 4))));

    // Then print some stuff (disabled to prevent debugging spew)
    // vector<Expr> print_args = vec<Expr>(3, 4.5f, new Cast(Int(8), 2), new Ramp(alpha, 3.2f, 4));
    // init = new Block(init, new PrintStmt("Test print: ", print_args));

    // Then run a parallel for loop that clobbers three elements of buf
    Expr e = new Select(alpha > 4.0f, 3, 2);
    e += (new Call(Int(32), "extern_function_1", vec<Expr>(alpha)));
    Stmt loop = new Store("buf", e, x + i);
    loop = new LetStmt("x", beta+1, loop);
    // Do some local allocations within the loop
    loop = new Allocate("tmp_stack", Int(32), 127, loop);
    loop = new Allocate("tmp_heap", Int(32), 43 * beta, loop);
    loop = new For("i", -1, 3, For::Parallel, loop);        

    Stmt s = new Block(init, loop);

    CodeGen_X86 cg;
    cg.compile(s, "test1", args);

    //cg.compile_to_bitcode("test1.bc");
    //cg.compile_to_native("test1.o", false);
    //cg.compile_to_native("test1.s", true);

    if (!getenv("HL_NUMTHREADS")) {
        #ifdef _WIN32
        putenv("HL_NUMTHREADS=4");
        #else
        setenv("HL_NUMTHREADS", "4", 1);
        #endif
    }
    JITCompiledModule m = cg.compile_to_function_pointers();
    typedef void (*fn_type)(::buffer_t *, float, int);
    fn_type fn = (fn_type)m.function;

    int scratch_buf[64];
    int *scratch = &scratch_buf[0];
    while (((size_t)scratch) & 0x1f) scratch++;
    ::buffer_t buf;
    memset(&buf, 0, sizeof(buf));
    buf.host = (uint8_t *)scratch;

    fn(&buf, -32, 0);
    assert(scratch[0] == 5);
    assert(scratch[1] == 5);
    assert(scratch[2] == 5);
    assert(scratch[3] == 3);
    assert(scratch[4] == 4*17);
    assert(scratch[5] == 5);
    assert(scratch[6] == 6*17);

    fn(&buf, 37.32f, 2);
    assert(scratch[0] == 0);
    assert(scratch[1] == 1);
    assert(scratch[2] == 4);
    assert(scratch[3] == 4);
    assert(scratch[4] == 4);
    assert(scratch[5] == 5);
    assert(scratch[6] == 6*17);

    fn(&buf, 4.0f, 1);
    assert(scratch[0] == 0);
    assert(scratch[1] == 3);
    assert(scratch[2] == 3);
    assert(scratch[3] == 3);
    assert(scratch[4] == 4*17);
    assert(scratch[5] == 5);
    assert(scratch[6] == 6*17);
    assert(extern_function_1_was_called);

    // Check the wrapped version does the same thing
    extern_function_1_was_called = false;
    for (int i = 0; i < 16; i++) scratch[i] = 0;

    float float_arg_val = 4.0f;
    int int_arg_val = 1;
    const void *arg_array[] = {&buf, &float_arg_val, &int_arg_val};
    m.wrapped_function(arg_array);
    assert(scratch[0] == 0);
    assert(scratch[1] == 3);
    assert(scratch[2] == 3);
    assert(scratch[3] == 3);
    assert(scratch[4] == 4*17);
    assert(scratch[5] == 5);
    assert(scratch[6] == 6*17);
    assert(extern_function_1_was_called);

    std::cout << "CodeGen_X86 test passed" << std::endl;
}

string CodeGen_X86::mcpu() const {
    if (use_avx) return "corei7-avx";
    if (use_sse_41) return "corei7";
    return "core2";
}

string CodeGen_X86::mattrs() const {
    return "";
}

}}
