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
#include "Simplify.h"
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
#include <llvm/Support/IRReader.h>
#else
#include <llvm/IR/Value.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Bitcode/ReaderWriter.h>
#endif

#include <llvm/Support/MemoryBuffer.h>

extern "C" unsigned char halide_internal_initmod_arm[];
extern "C" int halide_internal_initmod_arm_length;
extern "C" unsigned char halide_internal_initmod_arm_android[];
extern "C" int halide_internal_initmod_arm_android_length;

namespace Halide { 
namespace Internal {

using std::vector;
using std::string;
using std::ostringstream;

using namespace llvm;

CodeGen_ARM::CodeGen_ARM(bool android) : CodeGen_Posix(), use_android(android) {
    assert(llvm_ARM_enabled && "llvm build not configured with ARM target enabled.");
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
// cast operators
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

// saturating cast operators
Expr _i8q(Expr e) {
    return cast(Int(8, e.type().width), clamp(e, -128, 127));
}

Expr _u8q(Expr e) {
    if (e.type().is_uint()) {
        return cast(UInt(8, e.type().width), min(e, 255));
    } else {
        return cast(UInt(8, e.type().width), clamp(e, 0, 255));
    }
}

Expr _i16q(Expr e) {
    return cast(Int(16, e.type().width), clamp(e, -32768, 32767));
}

Expr _u16q(Expr e) {
    if (e.type().is_uint()) {
        return cast(UInt(16, e.type().width), min(e, 65535));
    } else {
        return cast(UInt(16, e.type().width), clamp(e, 0, 65535));
    }
}



}

Value *CodeGen_ARM::call_intrin(Type result_type, const string &name, vector<Expr> args) {    
    vector<Value *> arg_values(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        arg_values[i] = codegen(args[i]);
    }

    return call_intrin(llvm_type_of(result_type), name, arg_values);
}

Value *CodeGen_ARM::call_intrin(llvm::Type *result_type, 
                                const string &name, 
                                vector<Value *> arg_values) {
    vector<llvm::Type *> arg_types(arg_values.size());
    for (size_t i = 0; i < arg_values.size(); i++) {
        arg_types[i] = arg_values[i]->getType();
    }

    llvm::Function *fn = module->getFunction("llvm.arm.neon." + name);

    if (!fn) {
        FunctionType *func_t = FunctionType::get(result_type, arg_types, false);    
        fn = llvm::Function::Create(func_t, 
                                    llvm::Function::ExternalLinkage, 
                                    "llvm.arm.neon." + name, module);
        fn->setCallingConv(CallingConv::C);

        if (starts_with(name, "vld")) {
            fn->setOnlyReadsMemory();
            fn->setDoesNotCapture(1);
        } else {
            fn->setDoesNotAccessMemory();
        }
        fn->setDoesNotThrow();

    }

    log(4) << "Creating call to " << name << "\n";
    return builder->CreateCall(fn, arg_values, name);
}
 
void CodeGen_ARM::call_void_intrin(const string &name, vector<Expr> args) {    
    vector<Value *> arg_values(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        arg_values[i] = codegen(args[i]);
    }

    call_void_intrin(name, arg_values);
}


void CodeGen_ARM::call_void_intrin(const string &name, vector<Value *> arg_values) {
    vector<llvm::Type *> arg_types(arg_values.size());
    for (size_t i = 0; i < arg_values.size(); i++) {
        arg_types[i] = arg_values[i]->getType();
    }

    llvm::Function *fn = module->getFunction("llvm.arm.neon." + name);

    if (!fn) {
        FunctionType *func_t = FunctionType::get(void_t, arg_types, false);    
        fn = llvm::Function::Create(func_t, 
                                    llvm::Function::ExternalLinkage, 
                                    "llvm.arm.neon." + name, module);
        fn->setCallingConv(CallingConv::C);

        if (starts_with(name, "vst")) {
            fn->setDoesNotCapture(1);
        }
        fn->setDoesNotThrow();
    }

    log(4) << "Creating call to " << name << "\n";
    builder->CreateCall(fn, arg_values);    
}

void CodeGen_ARM::visit(const Cast *op) {

    vector<Expr> matches;
 
    struct Pattern {
        string intrin;
        Expr pattern;
        int type;
    };

    const int Simple = 0, LeftShift = 1, RightShift = 2;

    Pattern patterns[] = {
        {"vaddhn.v8i8", _i8((wild_i16x8 + wild_i16x8)/256), Simple},
        {"vaddhn.v4i16", _i16((wild_i32x4 + wild_i32x4)/65536), Simple},
        {"vaddhn.v8i8", _u8((wild_u16x8 + wild_u16x8)/256), Simple},
        {"vaddhn.v4i16", _u16((wild_u32x4 + wild_u32x4)/65536), Simple},
        {"vsubhn.v8i8", _i8((wild_i16x8 - wild_i16x8)/256), Simple},
        {"vsubhn.v4i16", _i16((wild_i32x4 - wild_i32x4)/65536), Simple},
        {"vsubhn.v8i8", _u8((wild_u16x8 - wild_u16x8)/256), Simple},
        {"vsubhn.v4i16", _u16((wild_u32x4 - wild_u32x4)/65536), Simple},
        {"vrhadds.v8i8", _i8((_i16(wild_i8x8) + _i16(wild_i8x8) + 1)/2), Simple},
        {"vrhaddu.v8i8", _u8((_u16(wild_u8x8) + _u16(wild_u8x8) + 1)/2), Simple},
        {"vrhadds.v4i16", _i16((_i32(wild_i16x4) + _i32(wild_i16x4) + 1)/2), Simple},
        {"vrhaddu.v4i16", _u16((_u32(wild_u16x4) + _u32(wild_u16x4) + 1)/2), Simple},
        {"vrhadds.v2i32", _i32((_i64(wild_i32x2) + _i64(wild_i32x2) + 1)/2), Simple},
        {"vrhaddu.v2i32", _u32((_u64(wild_u32x2) + _u64(wild_u32x2) + 1)/2), Simple},
        {"vrhadds.v16i8",   _i8((_i16(wild_i8x16) + _i16(wild_i8x16) + 1)/2), Simple},
        {"vrhaddu.v16i8",   _u8((_u16(wild_u8x16) + _u16(wild_u8x16) + 1)/2), Simple},
        {"vrhadds.v8i16", _i16((_i32(wild_i16x8) + _i32(wild_i16x8) + 1)/2), Simple},
        {"vrhaddu.v8i16", _u16((_u32(wild_u16x8) + _u32(wild_u16x8) + 1)/2), Simple},
        {"vrhadds.v4i32", _i32((_i64(wild_i32x4) + _i64(wild_i32x4) + 1)/2), Simple},
        {"vrhaddu.v4i32", _u32((_u64(wild_u32x4) + _u64(wild_u32x4) + 1)/2), Simple},

        {"vhadds.v8i8", _i8((_i16(wild_i8x8) + _i16(wild_i8x8))/2), Simple},
        {"vhaddu.v8i8", _u8((_u16(wild_u8x8) + _u16(wild_u8x8))/2), Simple},
        {"vhadds.v4i16", _i16((_i32(wild_i16x4) + _i32(wild_i16x4))/2), Simple},
        {"vhaddu.v4i16", _u16((_u32(wild_u16x4) + _u32(wild_u16x4))/2), Simple},
        {"vhadds.v2i32", _i32((_i64(wild_i32x2) + _i64(wild_i32x2))/2), Simple},
        {"vhaddu.v2i32", _u32((_u64(wild_u32x2) + _u64(wild_u32x2))/2), Simple},
        {"vhadds.v16i8", _i8((_i16(wild_i8x16) + _i16(wild_i8x16))/2), Simple},
        {"vhaddu.v16i8", _u8((_u16(wild_u8x16) + _u16(wild_u8x16))/2), Simple},
        {"vhadds.v8i16", _i16((_i32(wild_i16x8) + _i32(wild_i16x8))/2), Simple},
        {"vhaddu.v8i16", _u16((_u32(wild_u16x8) + _u32(wild_u16x8))/2), Simple},
        {"vhadds.v4i32", _i32((_i64(wild_i32x4) + _i64(wild_i32x4))/2), Simple},
        {"vhaddu.v4i32", _u32((_u64(wild_u32x4) + _u64(wild_u32x4))/2), Simple},
        {"vhsubs.v8i8", _i8((_i16(wild_i8x8) - _i16(wild_i8x8))/2), Simple},
        {"vhsubu.v8i8", _u8((_u16(wild_u8x8) - _u16(wild_u8x8))/2), Simple},
        {"vhsubs.v4i16", _i16((_i32(wild_i16x4) - _i32(wild_i16x4))/2), Simple},
        {"vhsubu.v4i16", _u16((_u32(wild_u16x4) - _u32(wild_u16x4))/2), Simple},
        {"vhsubs.v2i32", _i32((_i64(wild_i32x2) - _i64(wild_i32x2))/2), Simple},
        {"vhsubu.v2i32", _u32((_u64(wild_u32x2) - _u64(wild_u32x2))/2), Simple},
        {"vhsubs.v16i8", _i8((_i16(wild_i8x16) - _i16(wild_i8x16))/2), Simple},
        {"vhsubu.v16i8", _u8((_u16(wild_u8x16) - _u16(wild_u8x16))/2), Simple},
        {"vhsubs.v8i16", _i16((_i32(wild_i16x8) - _i32(wild_i16x8))/2), Simple},
        {"vhsubu.v8i16", _u16((_u32(wild_u16x8) - _u32(wild_u16x8))/2), Simple},
        {"vhsubs.v4i32", _i32((_i64(wild_i32x4) - _i64(wild_i32x4))/2), Simple},
        {"vhsubu.v4i32", _u32((_u64(wild_u32x4) - _u64(wild_u32x4))/2), Simple},

        {"vqadds.v8i8", _i8q(_i16(wild_i8x8) + _i16(wild_i8x8)), Simple},
        {"vqaddu.v8i8", _u8q(_u16(wild_u8x8) + _u16(wild_u8x8)), Simple},
        {"vqadds.v4i16", _i16q(_i32(wild_i16x4) + _i32(wild_i16x4)), Simple},
        {"vqaddu.v4i16", _u16q(_u32(wild_u16x4) + _u32(wild_u16x4)), Simple},
        {"vqadds.v16i8", _i8q(_i16(wild_i8x16) + _i16(wild_i8x16)), Simple},
        {"vqaddu.v16i8", _u8q(_u16(wild_u8x16) + _u16(wild_u8x16)), Simple},
        {"vqadds.v8i16", _i16q(_i32(wild_i16x8) + _i32(wild_i16x8)), Simple},
        {"vqaddu.v8i16", _u16q(_u32(wild_u16x8) + _u32(wild_u16x8)), Simple},

        // N.B. Saturating subtracts of unsigned types are expressed
        // by widening to a *signed* type
        {"vqsubs.v8i8", _i8q(_i16(wild_i8x8) - _i16(wild_i8x8)), Simple},
        {"vqsubu.v8i8", _u8q(_i16(wild_u8x8) - _i16(wild_u8x8)), Simple},
        {"vqsubs.v4i16", _i16q(_i32(wild_i16x4) - _i32(wild_i16x4)), Simple},
        {"vqsubu.v4i16", _u16q(_i32(wild_u16x4) - _i32(wild_u16x4)), Simple},
        {"vqsubs.v16i8", _i8q(_i16(wild_i8x16) - _i16(wild_i8x16)), Simple},
        {"vqsubu.v16i8", _u8q(_i16(wild_u8x16) - _i16(wild_u8x16)), Simple},
        {"vqsubs.v8i16", _i16q(_i32(wild_i16x8) - _i32(wild_i16x8)), Simple},
        {"vqsubu.v8i16", _u16q(_i32(wild_u16x8) - _i32(wild_u16x8)), Simple},

        {"vshiftn.v8i8", _i8(wild_i16x8/wild_i16x8), RightShift},
        {"vshiftn.v4i16", _i16(wild_i32x4/wild_i32x4), RightShift},
        {"vshiftn.v2i32", _i32(wild_i64x2/wild_i64x2), RightShift},
        {"vshiftn.v8i8", _u8(wild_u16x8/wild_u16x8), RightShift},
        {"vshiftn.v4i16", _u16(wild_u32x4/wild_u32x4), RightShift},
        {"vshiftn.v2i32", _u32(wild_u64x2/wild_u64x2), RightShift},

        {"vqshiftns.v8i8", _i8q(wild_i16x8/wild_i16x8), RightShift},
        {"vqshiftns.v4i16", _i16q(wild_i32x4/wild_i32x4), RightShift},
        {"vqshiftnu.v8i8", _u8q(wild_u16x8/wild_u16x8), RightShift},
        {"vqshiftnu.v4i16", _u16q(wild_u32x4/wild_u32x4), RightShift},
        {"vqshiftnsu.v8i8", _u8q(wild_i16x8/wild_i16x8), RightShift},
        {"vqshiftnsu.v4i16", _u16q(wild_i32x4/wild_i32x4), RightShift},

        {"vqshifts.v8i8", _i8q(_i16(wild_i8x8)*wild_i16x8), LeftShift},
        {"vqshifts.v4i16", _i16q(_i32(wild_i16x4)*wild_i32x4), LeftShift},
        {"vqshiftu.v8i8", _u8q(_u16(wild_u8x8)*wild_u16x8), LeftShift},
        {"vqshiftu.v4i16", _u16q(_u32(wild_u16x4)*wild_u32x4), LeftShift},
        {"vqshiftsu.v8i8", _u8q(_i16(wild_i8x8)*wild_i16x8), LeftShift},
        {"vqshiftsu.v4i16", _u16q(_i32(wild_i16x4)*wild_i32x4), LeftShift},
        {"vqshifts.v16i8", _i8q(_i16(wild_i8x16)*wild_i16x16), LeftShift},
        {"vqshifts.v8i16", _i16q(_i32(wild_i16x8)*wild_i32x8), LeftShift},
        {"vqshiftu.v16i8", _u8q(_u16(wild_u8x16)*wild_u16x16), LeftShift},
        {"vqshiftu.v8i16", _u16q(_u32(wild_u16x8)*wild_u32x8), LeftShift},
        {"vqshiftsu.v16i8", _u8q(_i16(wild_i8x16)*wild_i16x16), LeftShift},
        {"vqshiftsu.v8i16", _u16q(_i32(wild_i16x8)*wild_i32x8), LeftShift},

        {"vqmovns.v8i8", _i8q(wild_i16x8), Simple},
        {"vqmovns.v4i16", _i16q(wild_i32x4), Simple},
        {"vqmovnu.v8i8", _u8q(wild_u16x8), Simple},
        {"vqmovnu.v4i16", _u16q(wild_u32x4), Simple},
        {"vqmovnsu.v8i8", _u8q(wild_i16x8), Simple},
        {"vqmovnsu.v4i16", _u16q(wild_i32x4), Simple},

        {"sentinel", 0}

    };
        
    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        const Pattern &pattern = patterns[i];
        if (expr_match(pattern.pattern, op, matches)) {
            if (pattern.type == Simple) {
                value = call_intrin(pattern.pattern.type(), pattern.intrin, matches);
                return;
            } else { // must be a shift
                Expr constant = matches[1];
                int shift_amount;
                bool power_of_two = is_const_power_of_two(constant, &shift_amount);
                if (power_of_two && shift_amount < matches[0].type().bits) {
                    if (pattern.type == RightShift) {
                        shift_amount = -shift_amount;
                    }
                    Value *shift = ConstantInt::get(llvm_type_of(matches[0].type()), 
                                                    shift_amount);
                    value = call_intrin(llvm_type_of(pattern.pattern.type()), 
                                        pattern.intrin, 
                                        vec(codegen(matches[0]), shift));
                    return;
                }
            }
        }
    }

    CodeGen::visit(op);

}

void CodeGen_ARM::visit(const Mul *op) {  
    // We only have peephole optimizations for int vectors for now
    if (op->type.is_scalar() || op->type.is_float()) {
        CodeGen::visit(op);
        return;
    }

    // If the rhs is a power of two, consider a shift
    int shift_amount = 0;
    bool power_of_two = is_const_power_of_two(op->b, &shift_amount);
    const Cast *cast_a = op->a.as<Cast>();

    const Broadcast *broadcast_b = op->b.as<Broadcast>();
    Value *shift = NULL, *wide_shift = NULL;
    if (power_of_two) {
        if (cast_a) {
            wide_shift = ConstantInt::get(llvm_type_of(cast_a->value.type()), shift_amount);
        }        
        shift = ConstantInt::get(llvm_type_of(op->type), shift_amount);
    }

    // Widening left shifts
    if (power_of_two && cast_a && 
        cast_a->type == Int(16, 8) && cast_a->value.type() == Int(8, 8)) {
        Value *lhs = codegen(cast_a->value);
        value = call_intrin(i16x8, "vshiftls.v8i16", vec(lhs, wide_shift));
    } else if (power_of_two && cast_a && 
               cast_a->type == Int(32, 4) && cast_a->value.type() == Int(16, 4)) {
        Value *lhs = codegen(cast_a->value);
        value = call_intrin(i32x4, "vshiftls.v4i32", vec(lhs, wide_shift));
    } else if (power_of_two && cast_a && 
               cast_a->type == Int(64, 2) && cast_a->value.type() == Int(32, 2)) {
        Value *lhs = codegen(cast_a->value);
        value = call_intrin(i64x2, "vshiftls.v2i64", vec(lhs, wide_shift));
    } else if (power_of_two && cast_a && 
               cast_a->type == UInt(16, 8) && cast_a->value.type() == UInt(8, 8)) {
        Value *lhs = codegen(cast_a->value);
        value = call_intrin(i16x8, "vshiftlu.v8i16", vec(lhs, wide_shift));
    } else if (power_of_two && cast_a && 
               cast_a->type == UInt(32, 4) && cast_a->value.type() == UInt(16, 4)) {
        Value *lhs = codegen(cast_a->value);
        value = call_intrin(i32x4, "vshiftlu.v4i32", vec(lhs, wide_shift));
    } else if (power_of_two && cast_a && 
               cast_a->type == UInt(64, 2) && cast_a->value.type() == UInt(32, 2)) {
        Value *lhs = codegen(cast_a->value);
        value = call_intrin(i64x2, "vshiftlu.v2i64", vec(lhs, wide_shift));
    } else if (power_of_two && op->a.type() == Int(8, 8)) {
        // Non-widening left shifts
        Value *lhs = codegen(op->a);
        value = call_intrin(i8x8, "vshifts.v8i8", vec(lhs, shift));
    } else if (power_of_two && op->a.type() == Int(16, 4)) {
        Value *lhs = codegen(op->a);
        value = call_intrin(i16x4, "vshifts.v4i16", vec(lhs, shift));
    } else if (power_of_two && op->a.type() == Int(32, 2)) {
        Value *lhs = codegen(op->a);
        value = call_intrin(i32x2, "vshifts.v2i32", vec(lhs, shift));
    } else if (power_of_two && op->a.type() == Int(8, 16)) {
        Value *lhs = codegen(op->a);
        value = call_intrin(i8x16, "vshifts.v16i8", vec(lhs, shift));
    } else if (power_of_two && op->a.type() == Int(16, 8)) {
        Value *lhs = codegen(op->a);
        value = call_intrin(i16x8, "vshifts.v8i16", vec(lhs, shift));
    } else if (power_of_two && op->a.type() == Int(32, 4)) {
        Value *lhs = codegen(op->a);
        value = call_intrin(i32x4, "vshifts.v4i32", vec(lhs, shift));
    } else if (power_of_two && op->a.type() == Int(64, 2)) {
        Value *lhs = codegen(op->a);
        value = call_intrin(i64x2, "vshifts.v2i64", vec(lhs, shift));
    } else if (power_of_two && op->a.type() == UInt(8, 8)) {
        Value *lhs = codegen(op->a);
        value = call_intrin(i8x8, "vshiftu.v8i8", vec(lhs, shift));
    } else if (power_of_two && op->a.type() == UInt(16, 4)) {
        Value *lhs = codegen(op->a);
        value = call_intrin(i16x4, "vshiftu.v4i16", vec(lhs, shift));
    } else if (power_of_two && op->a.type() == UInt(32, 2)) {
        Value *lhs = codegen(op->a);
        value = call_intrin(i32x2, "vshiftu.v2i32", vec(lhs, shift));
    } else if (power_of_two && op->a.type() == UInt(8, 16)) {
        Value *lhs = codegen(op->a);
        value = call_intrin(i8x16, "vshiftu.v16i8", vec(lhs, shift));
    } else if (power_of_two && op->a.type() == UInt(16, 8)) {
        Value *lhs = codegen(op->a);
        value = call_intrin(i16x8, "vshiftu.v8i16", vec(lhs, shift));
    } else if (power_of_two && op->a.type() == UInt(32, 4)) {
        Value *lhs = codegen(op->a);
        value = call_intrin(i32x4, "vshiftu.v4i32", vec(lhs, shift));
    } else if (power_of_two && op->a.type() == UInt(64, 2)) {
        Value *lhs = codegen(op->a);
        value = call_intrin(i64x2, "vshiftu.v2i64", vec(lhs, shift));
    } else if (broadcast_b && is_const(broadcast_b->value, 3)) {        
        // Vector multiplies by 3, 5, 7, 9 should do shift-and-add or
        // shift-and-sub instead to reduce register pressure (the
        // shift is an immediate)
        value = codegen(op->a * 2 + op->a);        
    } else if (broadcast_b && is_const(broadcast_b->value, 5)) {
        value = codegen(op->a * 4 + op->a);
    } else if (broadcast_b && is_const(broadcast_b->value, 7)) {
        value = codegen(op->a * 8 - op->a);
    } else if (broadcast_b && is_const(broadcast_b->value, 9)) {
        value = codegen(op->a * 8 + op->a);
    } else {      
        CodeGen::visit(op);
    }
}

void CodeGen_ARM::visit(const Div *op) {    

    // First check if it's an averaging op
    struct Pattern {
        string op;
        Expr pattern;
    };
    Pattern averagings[] = {
        {"vhadds.v8i8", (wild_i8x8 + wild_i8x8)/2},
        {"vhaddu.v8i8", (wild_u8x8 + wild_u8x8)/2},
        {"vhadds.v4i16", (wild_i16x4 + wild_i16x4)/2},
        {"vhaddu.v4i16", (wild_u16x4 + wild_u16x4)/2},
        {"vhadds.v2i32", (wild_i32x2 + wild_i32x2)/2},
        {"vhaddu.v2i32", (wild_u32x2 + wild_u32x2)/2},
        {"vhadds.v16i8", (wild_i8x16 + wild_i8x16)/2},
        {"vhaddu.v16i8", (wild_u8x16 + wild_u8x16)/2},
        {"vhadds.v8i16", (wild_i16x8 + wild_i16x8)/2},
        {"vhaddu.v8i16", (wild_u16x8 + wild_u16x8)/2},
        {"vhadds.v4i32", (wild_i32x4 + wild_i32x4)/2},
        {"vhaddu.v4i32", (wild_u32x4 + wild_u32x4)/2},
        {"vhsubs.v8i8", (wild_i8x8 - wild_i8x8)/2},
        {"vhsubu.v8i8", (wild_u8x8 - wild_u8x8)/2},
        {"vhsubs.v4i16", (wild_i16x4 - wild_i16x4)/2},
        {"vhsubu.v4i16", (wild_u16x4 - wild_u16x4)/2},
        {"vhsubs.v2i32", (wild_i32x2 - wild_i32x2)/2},
        {"vhsubu.v2i32", (wild_u32x2 - wild_u32x2)/2},
        {"vhsubs.v16i8", (wild_i8x16 - wild_i8x16)/2},
        {"vhsubu.v16i8", (wild_u8x16 - wild_u8x16)/2},
        {"vhsubs.v8i16", (wild_i16x8 - wild_i16x8)/2},
        {"vhsubu.v8i16", (wild_u16x8 - wild_u16x8)/2},
        {"vhsubs.v4i32", (wild_i32x4 - wild_i32x4)/2},
        {"vhsubu.v4i32", (wild_u32x4 - wild_u32x4)/2}};
    
    if (is_two(op->b) && (op->a.as<Add>() || op->a.as<Sub>())) {
        vector<Expr> matches;
        for (size_t i = 0; i < sizeof(averagings)/sizeof(averagings[0]); i++) {
            if (expr_match(averagings[i].pattern, op, matches)) {
                value = call_intrin(matches[0].type(), averagings[i].op, matches);
                return;
            }
        }
    }

    // Detect if it's a small int division
    const Broadcast *broadcast = op->b.as<Broadcast>();
    const Cast *cast_b = broadcast ? broadcast->value.as<Cast>() : NULL;    
    const IntImm *int_imm = cast_b ? cast_b->value.as<IntImm>() : NULL;
    if (broadcast && !int_imm) int_imm = broadcast->value.as<IntImm>();
    int const_divisor = int_imm ? int_imm->value : 0;

    // Check if the divisor is a power of two
    int shift_amount;
    bool power_of_two = is_const_power_of_two(op->b, &shift_amount);

    vector<Expr> matches;    
    if (op->type == Float(32, 4) && is_one(op->a)) {
        // Reciprocal and reciprocal square root
        if (expr_match(new Call(Float(32, 4), "sqrt_f32", vec(wild_f32x4)), op->b, matches)) {
            value = call_intrin(Float(32, 4), "vrsqrte.v4f32", matches);
        } else {
            value = call_intrin(Float(32, 4), "vrecpe.v4f32", vec(op->b));
        }
    } else if (op->type == Float(32, 2) && is_one(op->a)) {
        // Reciprocal and reciprocal square root
        if (expr_match(new Call(Float(32, 2), "sqrt_f32", vec(wild_f32x2)), op->b, matches)) {
            value = call_intrin(Float(32, 2), "vrsqrte.v2f32", matches);
        } else {
            value = call_intrin(Float(32, 2), "vrecpe.v2f32", vec(op->b));
        }
    } else if (power_of_two && op->type.is_int()) {
        Value *numerator = codegen(op->a);
        Constant *shift = ConstantInt::get(llvm_type_of(op->type), shift_amount);
        value = builder->CreateAShr(numerator, shift);
    } else if (power_of_two && op->type.is_uint()) {
        Value *numerator = codegen(op->a);
        Constant *shift = ConstantInt::get(llvm_type_of(op->type), shift_amount);
        value = builder->CreateLShr(numerator, shift);
    } else if (op->type.element_of() == Int(16) && 
               const_divisor > 1 && const_divisor < 64) {
        int method     = IntegerDivision::table_s16[const_divisor-2][0];
        int multiplier = IntegerDivision::table_s16[const_divisor-2][1];
        int shift      = IntegerDivision::table_s16[const_divisor-2][2];        

        Expr e = op->a;

        // Start with multiply and keep high half
        if (method > 0) {
            Type wider = Int(32, op->type.width);
            e = cast(op->type, (cast(wider, e) * multiplier)/65536);

            // Possibly add a correcting factor
            if (method == 2) {
                e += (op->a - e) / 2;
            }
        }

        // Do the shift
        if (shift) {
            e /= (1 << shift);
        }

        value = codegen(e);
    } else if (op->type.element_of() == UInt(16) && 
               const_divisor > 1 && const_divisor < 64) {
        int method     = IntegerDivision::table_u16[const_divisor-2][0];
        int multiplier = IntegerDivision::table_u16[const_divisor-2][1];
        int shift      = IntegerDivision::table_u16[const_divisor-2][2];        

        Expr e = op->a;
        
        // Start with multiply and keep high half
        if (method > 0) {
            Type wider = UInt(32, op->type.width);
            e = cast(op->type, (cast(wider, e) * multiplier)/65536);

            // Possibly add a correcting factor
            if (method == 2) {
                e += (op->a - e) / 2;
            }
        }

        log(4) << "Performing shift\n";
        // Do the shift
        if (shift) {
            e /= (1 << shift);
        }

        value = codegen(e);

    } else {        

        CodeGen::visit(op);
    }
}

void CodeGen_ARM::visit(const Add *op) {
    CodeGen::visit(op);
}

void CodeGen_ARM::visit(const Sub *op) {

    CodeGen::visit(op);
}

void CodeGen_ARM::visit(const Min *op) {    

    if (op->type == Float(32)) {
        // Use a 2-wide vector instead
        Value *undef = UndefValue::get(f32x2);
        Constant *zero = ConstantInt::get(i32, 0);
        Value *a_wide = builder->CreateInsertElement(undef, codegen(op->a), zero);
        Value *b_wide = builder->CreateInsertElement(undef, codegen(op->b), zero);
        Value *wide_result = call_intrin(f32x2, "vmins.v2f32", vec(a_wide, b_wide));
        value = builder->CreateExtractElement(wide_result, zero);
        return;
    }

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

    if (op->type == Float(32)) {
        // Use a 2-wide vector instead
        Value *undef = UndefValue::get(f32x2);
        Constant *zero = ConstantInt::get(i32, 0);
        Value *a_wide = builder->CreateInsertElement(undef, codegen(op->a), zero);
        Value *b_wide = builder->CreateInsertElement(undef, codegen(op->b), zero);
        Value *wide_result = call_intrin(f32x2, "vmaxs.v2f32", vec(a_wide, b_wide));
        value = builder->CreateExtractElement(wide_result, zero);
        return;
    }

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

void CodeGen_ARM::visit(const Store *op) {
    // A dense store of an interleaving can be done using a vst2 intrinsic
    const Call *call = op->value.as<Call>();
    const Ramp *ramp = op->index.as<Ramp>();
    
    // We only deal with ramps here
    if (!ramp) {
        CodeGen::visit(op);
        return;
    }

    if (ramp && is_one(ramp->stride) && 
        call && call->name == "interleave vectors") {
        assert(call->args.size() == 2 && "Wrong number of args to interleave vectors");
        vector<Value *> args(call->args.size() + 2);

        Type t = call->args[0].type();
        int alignment = t.bits / 8;

        Value *index = codegen(ramp->base);
        Value *ptr = codegen_buffer_pointer(op->name, call->type.element_of(), index);
        ptr = builder->CreatePointerCast(ptr, i8->getPointerTo());  

        args[0] = ptr; // The pointer
        args[1] = codegen(call->args[0]);
        args[2] = codegen(call->args[1]);
        args[3] = ConstantInt::get(i32, alignment);

        if (t == Int(8, 8) || t == UInt(8, 8)) {
            call_void_intrin("vst2.v8i8", args);  
        } else if (t == Int(8, 16) || t == UInt(8, 16)) {
            call_void_intrin("vst2.v16i8", args);  
        } else if (t == Int(16, 4) || t == UInt(16, 4)) {
            call_void_intrin("vst2.v4i16", args);  
        } else if (t == Int(16, 8) || t == UInt(16, 8)) {
            call_void_intrin("vst2.v8i16", args);  
        } else if (t == Int(32, 2) || t == UInt(32, 2)) {
            call_void_intrin("vst2.v2i32", args);  
        } else if (t == Int(32, 4) || t == UInt(32, 4)) {
            call_void_intrin("vst2.v4i32", args); 
        } else if (t == Float(32, 2)) {
            call_void_intrin("vst2.v2f32", args); 
        } else if (t == Float(32, 4)) {
            call_void_intrin("vst2.v4f32", args); 
        } else {
            CodeGen::visit(op);
        }
        return;
    } 

    // If the stride is one or minus one, we can deal with that using vanilla codegen
    const IntImm *stride = ramp->stride.as<IntImm>();
    if (stride && (stride->value == 1 || stride->value == -1)) {
        CodeGen::visit(op);
        return;
    }

    // We have builtins for strided loads with fixed but unknown stride
    ostringstream builtin;
    builtin << "strided_store_"
            << (op->value.type().is_float() ? 'f' : 'i')
            << op->value.type().bits 
            << 'x' << op->value.type().width;

    llvm::Function *fn = module->getFunction(builtin.str());
    if (fn) {
        Value *base = codegen_buffer_pointer(op->name, op->value.type().element_of(), codegen(ramp->base));
        Value *stride = codegen(ramp->stride * (op->value.type().bits / 8));
        Value *val = codegen(op->value);
	log(4) << "Creating call to " << builtin.str() << "\n";
        builder->CreateCall(fn, vec(base, stride, val));
        return;
    }

    CodeGen::visit(op);

}

void CodeGen_ARM::visit(const Load *op) {
    const Ramp *ramp = op->index.as<Ramp>();

    // We only deal with ramps here
    if (!ramp) {
        CodeGen::visit(op);
        return;
    }

    const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;

    // If the stride is one or minus one, we can deal with that using vanilla codegen
    if (stride && (stride->value == 1 || stride->value == -1)) {
        CodeGen::visit(op);
        return;
    }

    // Strided loads with known stride
    if (stride && stride->value >= 2 && stride->value <= 4) {
        // Check alignment on the base. 
        Expr base = ramp->base;
        int offset = 0;
        ModulusRemainder mod_rem = modulus_remainder(ramp->base);

        
        if ((mod_rem.modulus % stride->value) == 0) {
            offset = mod_rem.remainder % stride->value;
            base = simplify(base - offset);
            mod_rem.remainder -= offset;
        }

        const Add *add = base.as<Add>();
        const IntImm *add_b = add ? add->b.as<IntImm>() : NULL;
        if ((mod_rem.modulus == 1) && add_b) {
            offset = add_b->value % stride->value;
            if (offset < 0) offset += stride->value;
            base = simplify(base - offset);
        }
        

        int alignment = op->type.bits / 8;
        //alignment *= gcd(gcd(mod_rem.modulus, mod_rem.remainder), 32);
        Value *align = ConstantInt::get(i32, alignment);

        Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), codegen(base));
        ptr = builder->CreatePointerCast(ptr, i8->getPointerTo());

        vector<llvm::Type *> type_vec(stride->value);
        llvm::Type *elem_type = llvm_type_of(op->type);
        for (int i = 0; i < stride->value; i++) {
            type_vec[i] = elem_type;
        }
        llvm::StructType *result_type = StructType::get(context, type_vec);

        ostringstream prefix;
        prefix << "vld" << stride->value << ".";
        string pre = prefix.str();

        Value *group = NULL;
        if (op->type == Int(8, 8) || op->type == UInt(8, 8)) {
            group = call_intrin(result_type, pre+"v8i8", vec(ptr, align));
        } else if (op->type == Int(16, 4) || op->type == UInt(16, 4)) {
            group = call_intrin(result_type, pre+"v4i16", vec(ptr, align));
        } else if (op->type == Int(32, 2) || op->type == UInt(32, 2)) {
            group = call_intrin(result_type, pre+"v2i32", vec(ptr, align));
        } else if (op->type == Float(32, 2)) {
            group = call_intrin(result_type, pre+"v2f32", vec(ptr, align));
        } else if (op->type == Int(8, 16) || op->type == UInt(8, 16)) {
            group = call_intrin(result_type, pre+"v16i8", vec(ptr, align));
        } else if (op->type == Int(16, 8) || op->type == UInt(16, 8)) {
            group = call_intrin(result_type, pre+"v8i16", vec(ptr, align));
        } else if (op->type == Int(32, 4) || op->type == UInt(32, 4)) {
            group = call_intrin(result_type, pre+"v4i32", vec(ptr, align));
        } else if (op->type == Float(32, 4)) {
            group = call_intrin(result_type, pre+"v4f32", vec(ptr, align));
        }

        if (group) {            
            log(4) << "Extracting element " << offset << " from resulting struct\n";
            value = builder->CreateExtractValue(group, vec((unsigned int)offset));            
            return;
        }
    }

    // We have builtins for strided loads with fixed but unknown stride
    ostringstream builtin;
    builtin << "strided_load_"
            << (op->type.is_float() ? 'f' : 'i')
            << op->type.bits 
            << 'x' << op->type.width;

    llvm::Function *fn = module->getFunction(builtin.str());
    if (fn) {
        Value *base = codegen_buffer_pointer(op->name, op->type.element_of(), codegen(ramp->base));
        Value *stride = codegen(ramp->stride * (op->type.bits / 8));
	log(4) << "Creating call to " << builtin.str() << "\n";
        value = builder->CreateCall(fn, vec(base, stride), builtin.str());
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
