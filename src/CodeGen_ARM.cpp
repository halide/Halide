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
#include "LLVM_Headers.h"

extern "C" unsigned char halide_internal_initmod_arm[];
extern "C" int halide_internal_initmod_arm_length;
extern "C" unsigned char halide_internal_initmod_arm_android[];
extern "C" int halide_internal_initmod_arm_android_length;

#if WITH_NATIVE_CLIENT
extern "C" unsigned char halide_internal_initmod_arm_nacl[];
extern "C" int halide_internal_initmod_arm_nacl_length;
#else
static void *halide_internal_initmod_arm_nacl = 0;
static int halide_internal_initmod_arm_nacl_length = 0;
#endif

namespace Halide { 
namespace Internal {

using std::vector;
using std::string;
using std::ostringstream;

using namespace llvm;

CodeGen_ARM::CodeGen_ARM(uint32_t options) : CodeGen_Posix(),
					     use_android(options & ARM_Android),
                                             use_nacl(options & ARM_NaCl) {
    assert(llvm_ARM_enabled && "llvm build not configured with ARM target enabled.");
    #if !(WITH_NATIVE_CLIENT)
    assert(!use_nacl && "llvm build not configured with native client enabled.");
    #endif
}

void CodeGen_ARM::compile(Stmt stmt, string name, const vector<Argument> &args) {

    init_module();

    StringRef sb;

    if (use_android) {
        assert(halide_internal_initmod_arm_android_length && 
               "initial module for arm_android is empty");
        sb = StringRef((char *)halide_internal_initmod_arm_android, 
                       halide_internal_initmod_arm_android_length);
    } else {
	if (use_nacl) {
            assert(halide_internal_initmod_arm_nacl_length && "initial module for ARM_nacl is empty");
            sb = StringRef((char *)halide_internal_initmod_arm_nacl, halide_internal_initmod_arm_nacl_length);
	} else {
	    assert(halide_internal_initmod_arm_length && "initial module for arm is empty");
	    sb = StringRef((char *)halide_internal_initmod_arm, halide_internal_initmod_arm_length);
	}
    }
    MemoryBuffer *bitcode_buffer = MemoryBuffer::getMemBuffer(sb);

    // Parse it    
    module = ParseBitcodeFile(bitcode_buffer, *context);

    // Fix the target triple. The initial module was probably compiled for x86

    log(1) << "Target triple of initial module: " << module->getTargetTriple() << "\n";
    if (use_android) {
        module->setTargetTriple("arm-linux-eabi");
    } else if (use_nacl) {
        module->setTargetTriple("arm-nacl");        
    } else {
        module->setTargetTriple("arm-linux-gnueabihf");
    }
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

Expr _i32q(Expr e) {
    return cast(Int(32, e.type().width), clamp(e, Int(32).min(), Int(32).max()));
}

Expr _u32q(Expr e) {
    if (e.type().is_uint()) {
        return cast(UInt(32, e.type().width), min(e, UInt(32).max()));
    } else {
        return cast(UInt(32, e.type().width), clamp(e, 0, UInt(32).max()));
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
        {"vqadds.v2i32", _i32q(_i64(wild_i32x2) + _i64(wild_i32x2)), Simple},
        {"vqaddu.v2i32", _u32q(_u64(wild_u32x2) + _u64(wild_u32x2)), Simple},
        {"vqadds.v16i8", _i8q(_i16(wild_i8x16) + _i16(wild_i8x16)), Simple},
        {"vqaddu.v16i8", _u8q(_u16(wild_u8x16) + _u16(wild_u8x16)), Simple},
        {"vqadds.v8i16", _i16q(_i32(wild_i16x8) + _i32(wild_i16x8)), Simple},
        {"vqaddu.v8i16", _u16q(_u32(wild_u16x8) + _u32(wild_u16x8)), Simple},
        {"vqadds.v4i32", _i32q(_i64(wild_i32x4) + _i64(wild_i32x4)), Simple},
        {"vqaddu.v4i32", _u32q(_u64(wild_u32x4) + _u64(wild_u32x4)), Simple},        

        // N.B. Saturating subtracts of unsigned types are expressed
        // by widening to a *signed* type
        {"vqsubs.v8i8", _i8q(_i16(wild_i8x8) - _i16(wild_i8x8)), Simple},
        {"vqsubu.v8i8", _u8q(_i16(wild_u8x8) - _i16(wild_u8x8)), Simple},
        {"vqsubs.v4i16", _i16q(_i32(wild_i16x4) - _i32(wild_i16x4)), Simple},
        {"vqsubu.v4i16", _u16q(_i32(wild_u16x4) - _i32(wild_u16x4)), Simple},
        {"vqsubs.v2i32", _i32q(_i64(wild_i32x2) - _i64(wild_i32x2)), Simple},
        {"vqsubu.v2i32", _u32q(_i64(wild_u32x2) - _i64(wild_u32x2)), Simple},
        {"vqsubs.v16i8", _i8q(_i16(wild_i8x16) - _i16(wild_i8x16)), Simple},
        {"vqsubu.v16i8", _u8q(_i16(wild_u8x16) - _i16(wild_u8x16)), Simple},
        {"vqsubs.v8i16", _i16q(_i32(wild_i16x8) - _i32(wild_i16x8)), Simple},
        {"vqsubu.v8i16", _u16q(_i32(wild_u16x8) - _i32(wild_u16x8)), Simple},
        {"vqsubs.v4i32", _i32q(_i64(wild_i32x4) - _i64(wild_i32x4)), Simple},        
        {"vqsubu.v4i32", _u32q(_i64(wild_u32x4) - _i64(wild_u32x4)), Simple},

        {"vshiftn.v8i8", _i8(wild_i16x8/wild_i16x8), RightShift},
        {"vshiftn.v4i16", _i16(wild_i32x4/wild_i32x4), RightShift},
        {"vshiftn.v2i32", _i32(wild_i64x2/wild_i64x2), RightShift},
        {"vshiftn.v8i8", _u8(wild_u16x8/wild_u16x8), RightShift},
        {"vshiftn.v4i16", _u16(wild_u32x4/wild_u32x4), RightShift},
        {"vshiftn.v2i32", _u32(wild_u64x2/wild_u64x2), RightShift},

        {"vqshiftns.v8i8", _i8q(wild_i16x8/wild_i16x8), RightShift},
        {"vqshiftns.v4i16", _i16q(wild_i32x4/wild_i32x4), RightShift},
        {"vqshiftns.v2i32", _i32q(wild_i64x2/wild_i64x2), RightShift},
        {"vqshiftnu.v8i8", _u8q(wild_u16x8/wild_u16x8), RightShift},
        {"vqshiftnu.v4i16", _u16q(wild_u32x4/wild_u32x4), RightShift},
        {"vqshiftnu.v2i32", _u32q(wild_u64x2/wild_u64x2), RightShift},
        {"vqshiftnsu.v8i8", _u8q(wild_i16x8/wild_i16x8), RightShift},
        {"vqshiftnsu.v4i16", _u16q(wild_i32x4/wild_i32x4), RightShift},
        {"vqshiftnsu.v2i32", _u32q(wild_i64x2/wild_i64x2), RightShift},

        {"vqshifts.v8i8", _i8q(_i16(wild_i8x8)*wild_i16x8), LeftShift},
        {"vqshifts.v4i16", _i16q(_i32(wild_i16x4)*wild_i32x4), LeftShift},
        {"vqshifts.v2i32", _i32q(_i64(wild_i32x2)*wild_i64x2), LeftShift},
        {"vqshiftu.v8i8", _u8q(_u16(wild_u8x8)*wild_u16x8), LeftShift},
        {"vqshiftu.v4i16", _u16q(_u32(wild_u16x4)*wild_u32x4), LeftShift},
        {"vqshiftu.v2i32", _u32q(_u64(wild_u32x2)*wild_u64x2), LeftShift},
        {"vqshiftsu.v8i8", _u8q(_i16(wild_i8x8)*wild_i16x8), LeftShift},
        {"vqshiftsu.v4i16", _u16q(_i32(wild_i16x4)*wild_i32x4), LeftShift},
        {"vqshiftsu.v2i32", _u32q(_i64(wild_i32x2)*wild_i64x2), LeftShift},
        {"vqshifts.v16i8", _i8q(_i16(wild_i8x16)*wild_i16x16), LeftShift},
        {"vqshifts.v8i16", _i16q(_i32(wild_i16x8)*wild_i32x8), LeftShift},
        {"vqshifts.v4i32", _i32q(_i64(wild_i32x4)*wild_i64x4), LeftShift},
        {"vqshiftu.v16i8", _u8q(_u16(wild_u8x16)*wild_u16x16), LeftShift},
        {"vqshiftu.v8i16", _u16q(_u32(wild_u16x8)*wild_u32x8), LeftShift},
        {"vqshiftu.v4i32", _u32q(_u64(wild_u32x4)*wild_u64x4), LeftShift},
        {"vqshiftsu.v16i8", _u8q(_i16(wild_i8x16)*wild_i16x16), LeftShift},
        {"vqshiftsu.v8i16", _u16q(_i32(wild_i16x8)*wild_i32x8), LeftShift},
        {"vqshiftsu.v4i32", _u32q(_i64(wild_i32x4)*wild_i64x4), LeftShift},

        {"vqmovns.v8i8", _i8q(wild_i16x8), Simple},
        {"vqmovns.v4i16", _i16q(wild_i32x4), Simple},
        {"vqmovns.v2i32", _i32q(wild_i64x2), Simple},
        {"vqmovnu.v8i8", _u8q(wild_u16x8), Simple},
        {"vqmovnu.v4i16", _u16q(wild_u32x4), Simple},
        {"vqmovnu.v2i32", _u32q(wild_u64x2), Simple},
        {"vqmovnsu.v8i8", _u8q(wild_i16x8), Simple},
        {"vqmovnsu.v4i16", _u16q(wild_i32x4), Simple},
        {"vqmovnsu.v2i32", _u32q(wild_i64x2), Simple},

        {"sentinel", 0}

    };
        
    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        const Pattern &pattern = patterns[i];
        //log(4) << "Trying pattern: " << patterns[i].intrin << " " << patterns[i].pattern << "\n";
        if (expr_match(pattern.pattern, op, matches)) {
            //log(4) << "Match!\n";
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

    // Vector multiplies by 3, 5, 7, 9 should do shift-and-add or
    // shift-and-sub instead to reduce register pressure (the
    // shift is an immediate)
    if (is_const(op->b, 3)) {
        value = codegen(op->a*2 + op->a);
        return;
    } else if (is_const(op->b, 5)) {
        value = codegen(op->a*4 + op->a);
        return;
    } else if (is_const(op->b, 7)) {
        value = codegen(op->a*8 - op->a);
        return;
    } else if (is_const(op->b, 9)) {
        value = codegen(op->a*8 + op->a);
        return;
    }

    vector<Expr> matches;
 
    struct Pattern {
        string intrin;
        Expr pattern;
    };

    Pattern const_rhs[] = {
        // {"intrinsic name", pattern, type}
        // Widening left shifts
        {"vshiftls.v8i16", _i16(wild_i8x8)*wild_i16x8},
        {"vshiftls.v4i32", _i32(wild_i16x4)*wild_i32x4},
        {"vshiftls.v2i64", _i64(wild_i32x2)*wild_i64x2},
        {"vshiftlu.v8i16", _u16(wild_u8x8)*wild_u16x8},
        {"vshiftlu.v4i32", _u32(wild_u16x4)*wild_u32x4},
        {"vshiftlu.v2i64", _u64(wild_u32x2)*wild_u64x2},
        // Non-widening left shifts
        {"vshifts.v16i8", wild_i8x16*wild_i8x16},
        {"vshifts.v8i16", wild_i16x8*wild_i16x8},
        {"vshifts.v4i32", wild_i32x4*wild_i32x4},
        {"vshifts.v2i64", wild_i64x2*wild_i64x2},        
        {"vshiftu.v16i8", wild_u8x16*wild_u8x16},
        {"vshiftu.v8i16", wild_u16x8*wild_u16x8},
        {"vshiftu.v4i32", wild_u32x4*wild_u32x4},
        {"vshiftu.v2i64", wild_u64x2*wild_u64x2},        
        {"vshifts.v8i8",  wild_i8x8*wild_i8x8},
        {"vshifts.v4i16", wild_i16x4*wild_i16x4},
        {"vshifts.v2i32", wild_i32x2*wild_i32x2},
        {"vshiftu.v8i8",  wild_u8x8*wild_u8x8},
        {"vshiftu.v4i16", wild_u16x4*wild_u16x4},
        {"vshiftu.v2i32", wild_u32x2*wild_u32x2},
    };

    int shift_amount = 0;
    bool power_of_two = is_const_power_of_two(op->b, &shift_amount);
    if (power_of_two) {
        for (size_t i = 0; i < sizeof(const_rhs)/sizeof(const_rhs[0]); i++) {
            const Pattern &pattern = const_rhs[i];
            if (expr_match(pattern.pattern, op, matches)) {
                llvm::Type *t_arg = llvm_type_of(matches[0].type());
                llvm::Type *t_result = llvm_type_of(pattern.pattern.type());
                Value *shift = ConstantInt::get(t_arg, shift_amount);
                value = call_intrin(t_result, pattern.intrin, vec(codegen(matches[0]), shift));
                return;
            }
        }
    }

    CodeGen::visit(op);
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
    if (!int_imm) int_imm = op->b.as<IntImm>();
    int const_divisor = int_imm ? int_imm->value : 0;

    // Check if the divisor is a power of two
    int shift_amount;
    bool power_of_two = is_const_power_of_two(op->b, &shift_amount);

    vector<Expr> matches;    
    if (op->type == Float(32, 4) && is_one(op->a)) {
        // Reciprocal and reciprocal square root
        if (expr_match(Call::make(Float(32, 4), "sqrt_f32", vec(wild_f32x4)), op->b, matches)) {
            value = call_intrin(Float(32, 4), "vrsqrte.v4f32", matches);
        } else {
            value = call_intrin(Float(32, 4), "vrecpe.v4f32", vec(op->b));
        }
    } else if (op->type == Float(32, 2) && is_one(op->a)) {
        // Reciprocal and reciprocal square root
        if (expr_match(Call::make(Float(32, 2), "sqrt_f32", vec(wild_f32x2)), op->b, matches)) {
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
    } else if (op->type.is_int() && 
               (op->type.bits == 32 || op->type.bits == 16 || op->type.bits == 8) && 
               const_divisor > 1 && 
               ((op->type.bits > 8 && const_divisor < 256) || const_divisor < 128)) {

        int64_t multiplier, shift;
        if (op->type.bits == 32) {
            multiplier = IntegerDivision::table_s32[const_divisor-2][1];
            shift      = IntegerDivision::table_s32[const_divisor-2][2];
        } else if (op->type.bits == 16) {
            multiplier = IntegerDivision::table_s16[const_divisor-2][1];
            shift      = IntegerDivision::table_s16[const_divisor-2][2];
        } else {
            // 8 bit
            multiplier = IntegerDivision::table_s8[const_divisor-2][1];
            shift      = IntegerDivision::table_s8[const_divisor-2][2];    
        }

        Value *val = codegen(op->a);
        
        // Make an all-ones mask if the numerator is negative
        Value *sign = builder->CreateAShr(val, codegen(make_const(op->type, op->type.bits-1)));            
        // Flip the numerator bits if the mask is high
        Value *flipped = builder->CreateXor(sign, val);
        // Grab the multiplier
        Value *mult = codegen(make_const(op->type, multiplier));
        // Widening multiply 
        llvm::Type *narrower = llvm_type_of(op->type);
        llvm::Type *wider = llvm_type_of(Int(op->type.bits*2, op->type.width));
        // flipped's high bit is zero, so it's ok to zero-extend it
        Value *flipped_wide = builder->CreateIntCast(flipped, wider, false);
        Value *mult_wide = builder->CreateIntCast(mult, wider, false);
        Value *wide_val = builder->CreateMul(flipped_wide, mult_wide);
        // Do the shift (add 8 or 16 to narrow back down)
        if (op->type == Int(32, 2) && shift == 0) {
            Constant *shift_amount = ConstantInt::get(wider, -32);
            val = call_intrin(narrower, "vshiftn.v2i32", vec<Value *>(wide_val, shift_amount));
        } else if (op->type == Int(16, 4) && shift == 0) {
            Constant *shift_amount = ConstantInt::get(wider, -16);
            val = call_intrin(narrower, "vshiftn.v4i16", vec<Value *>(wide_val, shift_amount));
        } else if (op->type == Int(8, 8) && shift == 0) {
            Constant *shift_amount = ConstantInt::get(wider, -8);
            val = call_intrin(narrower, "vshiftn.v8i8", vec<Value *>(wide_val, shift_amount));
        } else {
            Constant *shift_amount = ConstantInt::get(wider, (shift + op->type.bits));
            val = builder->CreateLShr(wide_val, shift_amount);
            val = builder->CreateIntCast(val, narrower, true);
        }
        // Maybe flip the bits again
        value = builder->CreateXor(val, sign);
    
    } else if (op->type.is_uint() && 
               (op->type.bits == 32 || op->type.bits == 16 || op->type.bits == 8) &&
               const_divisor > 1 && const_divisor < 256) {

        int64_t method, multiplier, shift;
        if (op->type.bits == 32) {
            method     = IntegerDivision::table_u32[const_divisor-2][0];
            multiplier = IntegerDivision::table_u32[const_divisor-2][1];
            shift      = IntegerDivision::table_u32[const_divisor-2][2];
        } else if (op->type.bits == 16) {        
            method     = IntegerDivision::table_u16[const_divisor-2][0];
            multiplier = IntegerDivision::table_u16[const_divisor-2][1];
            shift      = IntegerDivision::table_u16[const_divisor-2][2];
        } else {
            method     = IntegerDivision::table_u8[const_divisor-2][0];
            multiplier = IntegerDivision::table_u8[const_divisor-2][1];
            shift      = IntegerDivision::table_u8[const_divisor-2][2];
        }

        Value *num = codegen(op->a);

        // Widen
        llvm::Type *narrower = llvm_type_of(op->type);
        llvm::Type *wider = llvm_type_of(UInt(op->type.bits*2, op->type.width));
        Value *mult = ConstantInt::get(narrower, multiplier);
        mult = builder->CreateIntCast(mult, wider, false);
        Value *val = builder->CreateIntCast(num, wider, false);

        // Multiply
        val = builder->CreateMul(val, mult);

        // Narrow 
        if (op->type == UInt(32, 2) && shift == 0) {
            Constant *shift_amount = ConstantInt::get(wider, -32);
            val = call_intrin(narrower, "vshiftn.v2i32", vec<Value *>(val, shift_amount));
        } else if (op->type == UInt(16, 4) && shift == 0) {
            Constant *shift_amount = ConstantInt::get(wider, -16);
            val = call_intrin(narrower, "vshiftn.v4i16", vec<Value *>(val, shift_amount));
        } else if (op->type == UInt(8, 8) && shift == 0) {
            Constant *shift_amount = ConstantInt::get(wider, -8);
            val = call_intrin(narrower, "vshiftn.v8i8", vec<Value *>(val, shift_amount));
        } else {
            int shift_bits = op->type.bits;
            // For methods 0 and 1, we can do the final shift here too
            if (method != 2) {
                shift_bits += shift;
            }
            Constant *shift_amount = ConstantInt::get(wider, shift_bits);
            val = builder->CreateLShr(val, shift_amount);
            val = builder->CreateIntCast(val, narrower, false);
        }

        // Average with original numerator
        if (method == 2) {
            if (op->type == Int(32, 2)) {
                val = call_intrin(narrower, "vhaddu.v2i32", vec(val, num));
            } else if (op->type == Int(16, 4)) {
                val = call_intrin(narrower, "vhaddu.v4i16", vec(val, num));
            } else if (op->type == Int(8, 8)) {
                val = call_intrin(narrower, "vhaddu.v8i8", vec(val, num));
            } else {
                // num > val, so the following works without widening:
                // val += (num - val)/2
                Value *diff = builder->CreateSub(num, val);
                diff = builder->CreateLShr(diff, ConstantInt::get(diff->getType(), 1));
                val = builder->CreateAdd(val, diff);
            }
        
            // Do the final shift
            if (shift) {
                val = builder->CreateLShr(val, ConstantInt::get(narrower, shift));
            }
        }

        value = val;

    } else {        

        CodeGen::visit(op);
    }
}

void CodeGen_ARM::visit(const Add *op) {
    CodeGen::visit(op);
}

void CodeGen_ARM::visit(const Sub *op) {
    // Saturating negate
    struct Pattern {
        string op;
        Expr pattern;
    };
    Pattern patterns[] = {
        {"vqneg.v8i8", -max(wild_i8x8, -127)},
        {"vqneg.v16i8", -max(wild_i8x16, -127)},
        {"vqneg.v4i16", -max(wild_i16x4, -32767)},
        {"vqneg.v8i16", -max(wild_i16x8, -32767)},
        {"vqneg.v2i32", -max(wild_i32x2, -(0x7fffffff))},
        {"vqneg.v4i32", -max(wild_i32x4, -(0x7fffffff))}        
    };

    vector<Expr> matches;
    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        if (expr_match(patterns[i].pattern, op, matches)) {
            value = call_intrin(matches[0].type(), patterns[i].op, matches);
            return;
        }
    }

    // llvm will generate floating point negate instructions if we ask for (-0.0f)-x
    if (op->type.is_float() && is_zero(op->a)) {
        Constant *a;
        if (op->type.bits == 32) {
            a = ConstantFP::getNegativeZero(f32);
        } else if (op->type.bits == 64) {
            a = ConstantFP::getNegativeZero(f64);
        } else {
            a = NULL;
            assert(false && "Unknown bit width for floating point type");
        }

        Value *b = codegen(op->b);

        if (op->type.width > 1) {
            a = ConstantVector::getSplat(op->type.width, a);
        }
        value = builder->CreateFSub(a, b);
        return;
    }

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

    if (is_one(ramp->stride) && 
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
        llvm::StructType *result_type = StructType::get(*context, type_vec);

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
    return "cortex-a9";
}

string CodeGen_ARM::mattrs() const {
    return "+neon";
}

bool CodeGen_ARM::use_soft_float_abi() const {
    return use_android;
}

}}
