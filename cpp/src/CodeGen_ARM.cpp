#include "CodeGen_ARM.h"
#include "IROperator.h"
#include <iostream>
#include "buffer_t.h"
#include "IRPrinter.h"
#include "IRMatch.h"
#include "IREquality.h"
#include "Log.h"
#include "Util.h"
#include "Var.h"
#include "Param.h"
#include "integer_division_table.h"

#include <llvm/Config/config.h>

#if LLVM_VERSION_MINOR < 3
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


using std::vector;
using std::string;
using std::ostringstream;

extern "C" unsigned char halide_internal_initmod_arm[];
extern "C" int halide_internal_initmod_arm_length;
extern "C" unsigned char halide_internal_initmod_arm_android[];
extern "C" int halide_internal_initmod_arm_android_length;

namespace Halide { 
namespace Internal {


using namespace llvm;

CodeGen_ARM::CodeGen_ARM(bool android) : CodeGen_Posix(), use_android(android) {
}

void CodeGen_ARM::compile(Stmt stmt, string name, const vector<Argument> &args) {

    if (module && owns_module) delete module;

    StringRef sb;

    if (use_android) {
        assert(halide_internal_initmod_arm_android_length && 
               "initial module for arm_android is empty");
        sb = StringRef((char *)halide_internal_initmod_arm_android, 
                       halide_internal_initmod_arm_android_length);
    } else {
        assert(halide_internal_initmod_arm_length && "initial module for arm is empty");
        sb = StringRef((char *)halide_internal_initmod_arm, halide_internal_initmod_arm_length);
    }
    MemoryBuffer *bitcode_buffer = MemoryBuffer::getMemBuffer(sb);

    // Parse it    
    module = ParseBitcodeFile(bitcode_buffer, context);

    // Fix the target triple. The initial module was probably compiled for x86
    log(1) << "Target triple of initial module: " << module->getTargetTriple() << "\n";
    module->setTargetTriple("arm-linux-eabi");
    log(1) << "Target triple of initial module: " << module->getTargetTriple() << "\n";        

    // Pass to the generic codegen
    CodeGen::compile(stmt, name, args);
    delete bitcode_buffer;
}

namespace {
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
}

Value *CodeGen_ARM::call_intrin(Type result_type, const string &name, vector<Expr> args) {    
    vector<Value *> arg_values(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        arg_values[i] = codegen(args[i]);
    }

    return call_intrin(result_type, name, arg_values);
}

Value *CodeGen_ARM::call_intrin(Type result_type, const string &name, vector<Value *> arg_values) {
    vector<llvm::Type *> arg_types(arg_values.size());
    for (size_t i = 0; i < arg_values.size(); i++) {
        arg_types[i] = arg_values[i]->getType();
    }

    llvm::Function *fn = module->getFunction("llvm.arm.neon." + name);

    if (!fn) {
        FunctionType *func_t = FunctionType::get(llvm_type_of(result_type), arg_types, false);    
        fn = llvm::Function::Create(func_t, 
                                    llvm::Function::ExternalLinkage, 
                                    "llvm.arm.neon." + name, module);
        fn->setCallingConv(CallingConv::C);
    }

    return builder->CreateCall(fn, arg_values);
}
 
void CodeGen_ARM::visit(const Cast *op) {

    vector<Expr> matches;
 
    struct Pattern {
        string intrin;
        Expr pattern;
    };

    Pattern patterns[] = {
        {"vsubhn.v8i8", _i8((wild_i16x8 - wild_i16x8)/256)},
        {"vsubhn.v4i16", _i16((wild_i32x4 - wild_i32x4)/65536)},
        {"vsubhn.v8i8", _u8((wild_u16x8 - wild_u16x8)/256)},
        {"vsubhn.v4i16", _u16((wild_u32x4 - wild_u32x4)/65536)},
        {"sentinel", 0}
        /*
        {"sse2.padds.b", 
         _i8(clamp(_i16(wild_i8x16) + _i16(wild_i8x16), -128, 127))},
        {"sse2.psubs.b", 
         _i8(clamp(_i16(wild_i8x16) - _i16(wild_i8x16), -128, 127))},
        {"sse2.paddus.b", 
         _u8(min(_u16(wild_u8x16) + _u16(wild_u8x16), 255))},
        {"sse2.psubus.b", 
         _u8(max(_i16(wild_u8x16) - _i16(wild_u8x16), 0))},
        {"sse2.padds.w", 
         _i16(clamp(_i32(wild_i16x8) + _i32(wild_i16x8), -32768, 32767))},
        {"sse2.psubs.w", 
         _i16(clamp(_i32(wild_i16x8) - _i32(wild_i16x8), -32768, 32767))},
        {"sse2.paddus.w", 
         _u16(min(_u32(wild_u16x8) + _u32(wild_u16x8), 65535))},
        {"sse2.psubus.w", 
         _u16(max(_i32(wild_u16x8) - _i32(wild_u16x8), 0))},
        {"sse2.pmulh.w", 
         _i16((_i32(wild_i16x8) * _i32(wild_i16x8)) / 65536)},
        {"sse2.pmulhu.w", 
         _u16((_u32(wild_u16x8) * _u32(wild_u16x8)) / 65536)},
        {"sse2.pavg.b",
         _u8(((_u16(wild_u8x16) + _u16(wild_u8x16)) + 1) / 2)},
        {"sse2.pavg.w",
         _u16(((_u32(wild_u16x8) + _u32(wild_u16x8)) + 1) / 2)},
        {"packssdw", 
         _i16(clamp(wild_i32x8, -32768, 32767))},
        {"packsswb", 
         _i8(clamp(wild_i16x16, -128, 127))},
        {"packuswb", 
         _u8(clamp(wild_i16x16, 0, 255))},
        {"packusdw",
         _u16(clamp(wild_i32x8, 0, 65535))}
        */
    };
        
    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        const Pattern &pattern = patterns[i];
        if (expr_match(pattern.pattern, op, matches)) {
            value = call_intrin(pattern.pattern.type(), pattern.intrin, matches);
            return;
        }
    }

    CodeGen::visit(op);

}

void CodeGen_ARM::visit(const Div *op) {    

    /*

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
    */

    CodeGen::visit(op);
}

void CodeGen_ARM::visit(const Add *op) {
    CodeGen::visit(op);
}

void CodeGen_ARM::visit(const Sub *op) {

    CodeGen::visit(op);
}

void CodeGen_ARM::visit(const Min *op) {    

    struct {
        Type t;
        const char *op;
    } patterns[] = {
        {UInt(8, 8), "vminu.v8i8"},
        {UInt(8, 16), "vminu.v16i8"},
        {UInt(16, 4), "vminu.v4i16"},
        {UInt(16, 8), "vminu.v8i16"},
        {UInt(32, 2), "vminu.v2i32"},
        {UInt(32, 4), "vminu.v4i32"},
        {Int(8, 8), "vmins.v8i8"},
        {Int(8, 16), "vmins.v16i8"},
        {Int(16, 4), "vmins.v4i16"},
        {Int(16, 8), "vmins.v8i16"},
        {Int(32, 2), "vmins.v2i32"},
        {Int(32, 4), "vmins.v4i32"},
        {Float(32, 2), "vmins.v2f32"},
        {Float(32, 4), "vmins.v4f32"}
    };

    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        if (op->type == patterns[i].t) {
            value = call_intrin(op->type, patterns[i].op, vec(op->a, op->b));
            return;
        } 
    }

    CodeGen::visit(op);
}

void CodeGen_ARM::visit(const Max *op) {    

    struct {
        Type t;
        const char *op;
    } patterns[] = {
        {UInt(8, 8), "vmaxu.v8i8"},
        {UInt(8, 16), "vmaxu.v16i8"},
        {UInt(16, 4), "vmaxu.v4i16"},
        {UInt(16, 8), "vmaxu.v8i16"},
        {UInt(32, 2), "vmaxu.v2i32"},
        {UInt(32, 4), "vmaxu.v4i32"},
        {Int(8, 8), "vmaxs.v8i8"},
        {Int(8, 16), "vmaxs.v16i8"},
        {Int(16, 4), "vmaxs.v4i16"},
        {Int(16, 8), "vmaxs.v8i16"},
        {Int(32, 2), "vmaxs.v2i32"},
        {Int(32, 4), "vmaxs.v4i32"},
        {Float(32, 2), "vmaxs.v2f32"},
        {Float(32, 4), "vmaxs.v4f32"}
    };

    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        if (op->type == patterns[i].t) {
            value = call_intrin(op->type, patterns[i].op, vec(op->a, op->b));
            return;
        } 
    }

    CodeGen::visit(op);    
}

void CodeGen_ARM::visit(const LT *op) {
    const Call *a = op->a.as<Call>(), *b = op->b.as<Call>();
    
    if (a && b) {
        Constant *zero = ConstantVector::getSplat(op->type.width, ConstantInt::get(i32, 0));
        if (a->type == Float(32, 4) && 
            a->name == "abs_f32" && 
            b->name == "abs_f32") {            
            value = call_intrin(Int(32, 4), "vacgtq", vec(b->args[0], a->args[0]));            
            value = builder->CreateICmpNE(value, zero);
        } else if (a->type == Float(32, 2) && 
            a->name == "abs_f32" && 
            b->name == "abs_f32") {            
            value = call_intrin(Int(32, 2), "vacgtd", vec(b->args[0], a->args[0]));            
            value = builder->CreateICmpNE(value, zero);
        } else {
            CodeGen::visit(op);
        }
    } else {
        CodeGen::visit(op);
    }
    
}

void CodeGen_ARM::visit(const LE *op) {
    const Call *a = op->a.as<Call>(), *b = op->b.as<Call>();
    
    if (a && b) {
        Constant *zero = ConstantVector::getSplat(op->type.width, ConstantInt::get(i32, 0));
        if (a->type == Float(32, 4) && 
            a->name == "abs_f32" && 
            b->name == "abs_f32") {            
            value = call_intrin(Int(32, 4), "vacgeq", vec(b->args[0], a->args[0]));            
            value = builder->CreateICmpNE(value, zero);            
        } else if (a->type == Float(32, 2) && 
            a->name == "abs_f32" && 
            b->name == "abs_f32") {            
            value = call_intrin(Int(32, 2), "vacged", vec(b->args[0], a->args[0]));            
            value = builder->CreateICmpNE(value, zero);
        } else {
            CodeGen::visit(op);
        }
    } else {
        CodeGen::visit(op);
    }
    
}

void CodeGen_ARM::visit(const Select *op) {

    // Absolute difference patterns:
    // select(a < b, b - a, a - b)    
    const LT *cmp = op->condition.as<LT>();    
    const Sub *a = op->true_value.as<Sub>();
    const Sub *b = op->false_value.as<Sub>();
    Type t = op->type;

    int vec_bits = t.bits * t.width;

    if (cmp && a && b && 
        equal(a->a, b->b) &&
        equal(a->b, b->a) &&
        equal(cmp->a, a->b) &&
        equal(cmp->b, a->a) &&
        (!t.is_float()) && 
        (t.bits == 8 || t.bits == 16 || t.bits == 32 || t.bits == 64) &&
        (vec_bits == 64 || vec_bits == 128)) {

        ostringstream ss;

        // If cmp->a and cmp->b are both widening casts of a narrower
        // int, we can use vadbl instead of vabd. llvm reaches vabdl
        // by expecting you to widen the result of a narrower vabd.
        const Cast *ca = cmp->a.as<Cast>();
        const Cast *cb = cmp->b.as<Cast>();
        if (ca && cb && vec_bits == 128 &&
            ca->value.type().bits * 2 == t.bits &&
            cb->value.type().bits * 2 == t.bits &&
            ca->value.type().t == t.t &&
            cb->value.type().t == t.t) {
            ss << "vabd" << (t.is_int() ? "s" : "u") << ".v" << t.width << "i" << t.bits/2;
            value = call_intrin(ca->value.type(), ss.str(), vec(ca->value, cb->value));
            value = builder->CreateIntCast(value, llvm_type_of(t), false);
        } else {
            ss << "vabd" << (t.is_int() ? "s" : "u") << ".v" << t.width << "i" << t.bits;
            value = call_intrin(t, ss.str(), vec(cmp->a, cmp->b));
        }

        return;
    }

    CodeGen::visit(op);
}

string CodeGen_ARM::mcpu() const {
    return "cortex-a8";
}

string CodeGen_ARM::mattrs() const {
    return "+neon";
}

}}
