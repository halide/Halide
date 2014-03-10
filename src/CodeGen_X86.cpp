#include <iostream>

#include "CodeGen_X86.h"
#include "IROperator.h"
#include "buffer_t.h"
#include "IRMatch.h"
#include "Debug.h"
#include "Util.h"
#include "Var.h"
#include "Param.h"
#include "IntegerDivisionTable.h"
#include "LLVM_Headers.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_X86::CodeGen_X86(Target t) : CodeGen_Posix(),
                                     target(t) {
    #if !(WITH_X86)
    assert(false && "x86 not enabled for this build of Halide.");
    #endif

    assert(llvm_X86_enabled && "llvm build not configured with X86 target enabled.");

    #if !(WITH_NATIVE_CLIENT)
    assert(t.os != Target::NaCl && "llvm build not configured with native client enabled.");
    #endif
}

llvm::Triple CodeGen_X86::get_target_triple() const {
    llvm::Triple triple;

    if (target.bits == 32) {
        triple.setArch(llvm::Triple::x86);
    } else {
        assert(target.bits == 64);
        triple.setArch(llvm::Triple::x86_64);
    }

    // Fix the target triple

    if (target.os == Target::Linux) {
        triple.setOS(llvm::Triple::Linux);
        triple.setEnvironment(llvm::Triple::GNU);
    } else if (target.os == Target::OSX) {
        triple.setVendor(llvm::Triple::Apple);
        triple.setOS(llvm::Triple::MacOSX);
    } else if (target.os == Target::Windows) {
        triple.setVendor(llvm::Triple::PC);
        triple.setOS(llvm::Triple::Win32);
        if (target.features & Target::JIT) {
            // Use ELF for jitting
            #if LLVM_VERSION < 35
            triple.setEnvironment(llvm::Triple::ELF);
            #else
            triple.setObjectFormat(llvm::Triple::ELF);
            #endif
        }
    } else if (target.os == Target::Android) {
        triple.setOS(llvm::Triple::Linux);
        triple.setEnvironment(llvm::Triple::Android);

        if (target.bits == 64) {
            std::cerr << "Warning: x86-64 android is untested\n";
        }
    } else if (target.os == Target::NaCl) {
        #if WITH_NATIVE_CLIENT
        triple.setOS(llvm::Triple::NaCl);
        triple.setEnvironment(llvm::Triple::GNU);
        #else
        assert(false && "This version of Halide was compiled without nacl support");
        #endif
    } else if (target.os == Target::IOS) {
        assert(false && "Not sure what llvm target triple to use when compiling to IOS on x86 (does this even exist?)");
    }

    return triple;
}

void CodeGen_X86::compile(Stmt stmt, string name,
                          const vector<Argument> &args,
                          const vector<Buffer> &images_to_embed) {

    init_module();

    // Fix the target triple
    module = get_initial_module_for_target(target, context);

    llvm::Triple triple = get_target_triple();
    module->setTargetTriple(triple.str());

    debug(1) << "Target triple of initial module: " << module->getTargetTriple() << "\n";

    // Pass to the generic codegen
    CodeGen::compile(stmt, name, args, images_to_embed);

    // Optimize
    CodeGen::optimize_module();
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

    return call_intrin(llvm_type_of(result_type), name, arg_values);
}

Value *CodeGen_X86::call_intrin(llvm::Type *result_type, const string &name, vector<Value *> arg_values) {
    vector<llvm::Type *> arg_types(arg_values.size());
    for (size_t i = 0; i < arg_values.size(); i++) {
        arg_types[i] = arg_values[i]->getType();
    }

    llvm::Function *fn = module->getFunction("llvm.x86." + name);

    if (!fn) {
        FunctionType *func_t = FunctionType::get(result_type, arg_types, false);
        fn = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, "llvm.x86." + name, module);
        fn->setCallingConv(CallingConv::C);
    }

    CallInst *call = builder->CreateCall(fn, arg_values);
    call->setDoesNotAccessMemory();
    call->setDoesNotThrow();

    return call;
}

namespace {

// Attempt to cast an expression to a smaller type while provably not
// losing information. If it can't be done, return an undefined Expr.
Expr lossless_cast(Type t, Expr e) {
    if (t.can_represent(e.type())) {
        return e;
    }

    if (const Cast *c = e.as<Cast>()) {
        if (t == c->value.type()) {
            return c->value;
        } else if (t.can_represent(c->value.type())) {
            return cast(t, c->value);
        } else {
            return Expr();
        }
    }

    if (const Broadcast *b = e.as<Broadcast>()) {
        Expr v = lossless_cast(t.element_of(), b->value);
        if (v.defined()) {
            return Broadcast::make(v, b->width);
        } else {
            return Expr();
        }
    }

    if (const IntImm *i = e.as<IntImm>()) {
        int x = int_cast_constant(t, i->value);
        if (x == i->value) {
            return e;
        } else {
            return Expr();
        }
    }

    return Expr();
}

}

void CodeGen_X86::visit(const Cast *op) {

    vector<Expr> matches;

    struct Pattern {
        bool needs_sse_41;
        bool extern_call;
        bool wide_op;
        Type type;
        string intrin;
        Expr pattern;
    };

    Pattern patterns[] = {
        {false, false, true, Int(8, 16), "sse2.padds.b",
         _i8(clamp(wild_i16x16 + wild_i16x16, -128, 127))},
        {false, false, true, Int(8, 16), "sse2.psubs.b",
         _i8(clamp(wild_i16x16 - wild_i16x16, -128, 127))},
        {false, false, true, UInt(8, 16), "sse2.paddus.b",
         _u8(min(wild_u16x16 + wild_u16x16, 255))},
        {false, false, true, UInt(8, 16), "sse2.psubus.b",
         _u8(max(wild_i16x16 - wild_i16x16, 0))},
        {false, false, true, Int(16, 8), "sse2.padds.w",
         _i16(clamp(wild_i32x8 + wild_i32x8, -32768, 32767))},
        {false, false, true, Int(16, 8), "sse2.psubs.w",
         _i16(clamp(wild_i32x8 - wild_i32x8, -32768, 32767))},
        {false, false, true, UInt(16, 8), "sse2.paddus.w",
         _u16(min(wild_u32x8 + wild_u32x8, 65535))},
        {false, false, true, UInt(16, 8), "sse2.psubus.w",
         _u16(max(wild_i32x8 - wild_i32x8, 0))},
        {false, false, true, Int(16, 8), "sse2.pmulh.w",
         _i16((wild_i32x8 * wild_i32x8) / 65536)},
        {false, false, true, UInt(16, 8), "sse2.pmulhu.w",
         _u16((wild_u32x8 * wild_u32x8) / 65536)},
        {false, false, true, UInt(8, 16), "sse2.pavg.b",
         _u8(((wild_u16x16 + wild_u16x16) + 1) / 2)},
        {false, false, true, UInt(16, 8), "sse2.pavg.w",
         _u16(((wild_u32x8 + wild_u32x8) + 1) / 2)},
        {false, true, false, Int(16, 8), "packssdw",
         _i16(clamp(wild_i32x8, -32768, 32767))},
        {false, true, false, Int(8, 16), "packsswb",
         _i8(clamp(wild_i16x16, -128, 127))},
        {false, true, false, UInt(8, 16), "packuswb",
         _u8(clamp(wild_i16x16, 0, 255))},
        {true, true, false, UInt(16, 8), "packusdw",
         _u16(clamp(wild_i32x8, 0, 65535))}
    };

    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        const Pattern &pattern = patterns[i];
        if (!(target.features | Target::SSE41) && pattern.needs_sse_41) continue;
        if (expr_match(pattern.pattern, op, matches)) {
            bool ok = true;
            if (pattern.wide_op) {
                // Try to narrow the matches to the target type.
                for (size_t i = 0; i < matches.size(); i++) {
                    matches[i] = lossless_cast(pattern.type, matches[i]);
                    if (!matches[i].defined()) ok = false;
                }
            }
            if (!ok) continue;

            if (pattern.extern_call) {
                value = codegen(Call::make(pattern.type, pattern.intrin, matches, Call::Extern));
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

    assert(!is_zero(op->b) && "Division by constant zero");

    // Detect if it's a small int division
    const Broadcast *broadcast = op->b.as<Broadcast>();
    const Cast *cast_b = broadcast ? broadcast->value.as<Cast>() : NULL;
    const IntImm *int_imm = cast_b ? cast_b->value.as<IntImm>() : NULL;
    if (broadcast && !int_imm) int_imm = broadcast->value.as<IntImm>();
    if (!int_imm) int_imm = op->b.as<IntImm>();
    int const_divisor = int_imm ? int_imm->value : 0;
    int shift_amount;
    bool power_of_two = is_const_power_of_two(op->b, &shift_amount);

    vector<Expr> matches;
    if (op->type == Float(32, 4) && is_one(op->a)) {
        // Reciprocal and reciprocal square root
        if (expr_match(Call::make(Float(32, 4), "sqrt_f32", vec(wild_f32x4), Call::Extern), op->b, matches)) {
            value = call_intrin(Float(32, 4), "sse.rsqrt.ps", matches);
        } else {
            value = call_intrin(Float(32, 4), "sse.rcp.ps", vec(op->b));
        }
    } else if ((target.features & Target::AVX) && op->type == Float(32, 8) && is_one(op->a)) {
        // Reciprocal and reciprocal square root
        if (expr_match(Call::make(Float(32, 8), "sqrt_f32", vec(wild_f32x8), Call::Extern), op->b, matches)) {
            value = call_intrin(Float(32, 8), "avx.rsqrt.ps.256", matches);
        } else {
            value = call_intrin(Float(32, 8), "avx.rcp.ps.256", vec(op->b));
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
               (op->type.bits == 8 || op->type.bits == 16 || op->type.bits == 32) &&
               const_divisor > 1 &&
               ((op->type.bits > 8 && const_divisor < 256) || const_divisor < 128)) {

        int64_t multiplier, shift;
        if (op->type.bits == 32) {
            multiplier = IntegerDivision::table_s32[const_divisor][2];
            shift      = IntegerDivision::table_s32[const_divisor][3];
        } else if (op->type.bits == 16) {
            multiplier = IntegerDivision::table_s16[const_divisor][2];
            shift      = IntegerDivision::table_s16[const_divisor][3];
        } else {
            // 8 bit
            multiplier = IntegerDivision::table_s8[const_divisor][2];
            shift      = IntegerDivision::table_s8[const_divisor][3];
        }

        Value *val = codegen(op->a);

        // Make an all-ones mask if the numerator is negative
        Value *sign = builder->CreateAShr(val, codegen(make_const(op->type, op->type.bits-1)));
        // Flip the numerator bits if the mask is high.
        Value *flipped = builder->CreateXor(sign, val);

        llvm::Type *narrower = llvm_type_of(op->type);
        llvm::Type *wider = llvm_type_of(Int(op->type.bits*2, op->type.width));

        // Grab the multiplier.
        Value *mult = ConstantInt::get(narrower, multiplier);

        // Widening multiply, keep high half, shift
        if (op->type == Int(16, 8)) {
            val = call_intrin(narrower, "sse2.pmulhu.w", vec(flipped, mult));
            if (shift) {
                Constant *shift_amount = ConstantInt::get(narrower, shift);
                val = builder->CreateLShr(val, shift_amount);
            }
        } else {
            // flipped's high bit is zero, so it's ok to zero-extend it
            Value *flipped_wide = builder->CreateIntCast(flipped, wider, false);
            Value *mult_wide = builder->CreateIntCast(mult, wider, false);
            Value *wide_val = builder->CreateMul(flipped_wide, mult_wide);
            // Do the shift (add 8 or 16 or 32 to narrow back down)
            Constant *shift_amount = ConstantInt::get(wider, (shift + op->type.bits));
            val = builder->CreateLShr(wide_val, shift_amount);
            val = builder->CreateIntCast(val, narrower, true);
        }

        // Maybe flip the bits again
        value = builder->CreateXor(val, sign);

    } else if (op->type.is_uint() &&
               (op->type.bits == 8 || op->type.bits == 16 || op->type.bits == 32) &&
               const_divisor > 1 && const_divisor < 256) {

        int64_t method, multiplier, shift;
        if (op->type.bits == 32) {
            method     = IntegerDivision::table_u32[const_divisor][1];
            multiplier = IntegerDivision::table_u32[const_divisor][2];
            shift      = IntegerDivision::table_u32[const_divisor][3];
        } else if (op->type.bits == 16) {
            method     = IntegerDivision::table_u16[const_divisor][1];
            multiplier = IntegerDivision::table_u16[const_divisor][2];
            shift      = IntegerDivision::table_u16[const_divisor][3];
        } else {
            method     = IntegerDivision::table_u8[const_divisor][1];
            multiplier = IntegerDivision::table_u8[const_divisor][2];
            shift      = IntegerDivision::table_u8[const_divisor][3];
        }

        assert(method != 0 &&
               "method 0 division is for powers of two and should have been handled elsewhere");

        Value *num = codegen(op->a);

        // Widen, multiply, narrow
        llvm::Type *narrower = llvm_type_of(op->type);
        llvm::Type *wider = llvm_type_of(UInt(op->type.bits*2, op->type.width));

        Value *mult = ConstantInt::get(narrower, multiplier);
        Value *val = num;

        if (op->type == UInt(16, 8)) {
            val = call_intrin(narrower, "sse2.pmulhu.w", vec(val, mult));
            if (shift && method == 1) {
                Constant *shift_amount = ConstantInt::get(narrower, shift);
                val = builder->CreateLShr(val, shift_amount);
            }
        } else {

            // Widen
            mult = builder->CreateIntCast(mult, wider, false);
            val = builder->CreateIntCast(val, wider, false);

            // Multiply
            val = builder->CreateMul(val, mult);

            // Keep high half
            int shift_bits = op->type.bits;
            // For method 1, we can do the final shift here too
            if (method == 1) {
                shift_bits += (int)shift;
            }
            Constant *shift_amount = ConstantInt::get(wider, shift_bits);
            val = builder->CreateLShr(val, shift_amount);
            val = builder->CreateIntCast(val, narrower, false);
        }

        // Average with original numerator. Can't use sse rounding ops
        // because they round up.
        if (method == 2) {
            // num > val, so the following works without widening:
            // val += (num - val)/2
            Value *diff = builder->CreateSub(num, val);
            diff = builder->CreateLShr(diff, ConstantInt::get(diff->getType(), 1));
            val = builder->CreateNSWAdd(val, diff);

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

void CodeGen_X86::visit(const Min *op) {
    bool use_sse_41 = target.features & Target::SSE41;
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
    bool use_sse_41 = target.features & Target::SSE41;
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
    Stmt init = For::make("i", 0, 3, For::Serial,
                          Store::make("buf",
                                      Ramp::make(i*4+2, 1, 4),
                                      Ramp::make(i*4+2, 1, 4)));

    // Now set the first two elements using scalars, and last four elements using a dense aligned vector
    init = Block::make(init, Store::make("buf", 0, 0));
    init = Block::make(init, Store::make("buf", 1, 1));
    init = Block::make(init, Store::make("buf", Ramp::make(12, 1, 4), Ramp::make(12, 1, 4)));

    // Then multiply the even terms by 17 using sparse vectors
    init = Block::make(init,
                       For::make("i", 0, 2, For::Serial,
                                 Store::make("buf",
                                             Mul::make(Broadcast::make(17, 4),
                                                       Load::make(Int(32, 4), "buf",
                                                                  Ramp::make(i*8, 2, 4), Buffer(), Parameter())),
                                             Ramp::make(i*8, 2, 4))));

    // Then print some stuff (disabled to prevent debugging spew)
    // vector<Expr> print_args = vec<Expr>(3, 4.5f, Cast::make(Int(8), 2), Ramp::make(alpha, 3.2f, 4));

    // Then run a parallel for loop that clobbers three elements of buf
    Expr e = Select::make(alpha > 4.0f, 3, 2);
    e += (Call::make(Int(32), "extern_function_1", vec<Expr>(alpha), Call::Extern));
    Stmt loop = Store::make("buf", e, x + i);
    loop = LetStmt::make("x", beta+1, loop);
    // Do some local allocations within the loop
    loop = Allocate::make("tmp_stack", Int(32), vec(Expr(127)), Block::make(loop, Free::make("tmp_stack")));
    loop = Allocate::make("tmp_heap", Int(32), vec(Expr(43), Expr(beta)), Block::make(loop, Free::make("tmp_heap")));
    loop = For::make("i", -1, 3, For::Parallel, loop);

    Stmt s = Block::make(init, loop);

    CodeGen_X86 cg(get_host_target());
    cg.compile(s, "test1", args, vector<Buffer>());

    //cg.compile_to_bitcode("test1.bc");
    //cg.compile_to_native("test1.o", false);
    //cg.compile_to_native("test1.s", true);

    #ifdef _WIN32
    {
        char buf[32];
        size_t read;
        getenv_s(&read, buf, "HL_NUMTHREADS");
        if (read == 0) putenv("HL_NUMTHREADS=4");
    }
    #else
    if (!getenv("HL_NUMTHREADS")) {
        setenv("HL_NUMTHREADS", "4", 1);
    }
    #endif

    debug(2) << "Compiling to function pointers \n";
    JITCompiledModule m = cg.compile_to_function_pointers();

    typedef int (*fn_type)(::buffer_t *, float, int);
    fn_type fn = reinterpret_bits<fn_type>(m.function);

    debug(2) << "Function pointer lives at " << m.function << "\n";

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
    if (target.features & Target::AVX) return "corei7-avx";
    // We want SSE4.1 but not SSE4.2, hence "penryn" rather than "corei7"
    if (target.features & Target::SSE41) return "penryn";
    // Default should not include SSSE3, hence "k8" rather than "core2"
    return "k8";
}

string CodeGen_X86::mattrs() const {
    return "";
}

bool CodeGen_X86::use_soft_float_abi() const {
    return false;
}

}}
