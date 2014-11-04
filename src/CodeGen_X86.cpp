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
#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

CodeGen_X86::CodeGen_X86(Target t) : CodeGen_Posix(t),
                                     jitEventListener(NULL) {

    #if !(WITH_X86)
    user_error << "x86 not enabled for this build of Halide.\n";
    #endif

    user_assert(llvm_X86_enabled) << "llvm build not configured with X86 target enabled.\n";

    #if !(WITH_NATIVE_CLIENT)
    user_assert(t.os != Target::NaCl) << "llvm build not configured with native client enabled.\n";
    #endif
}

llvm::Triple CodeGen_X86::get_target_triple() const {
    llvm::Triple triple;

    if (target.bits == 32) {
        triple.setArch(llvm::Triple::x86);
    } else {
        user_assert(target.bits == 64) << "Target must be 32- or 64-bit.\n";
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
#if LLVM_VERSION >= 36
        triple.setEnvironment(llvm::Triple::MSVC);
#endif
        if (target.has_feature(Target::JIT)) {
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
        #ifdef WITH_NATIVE_CLIENT
        triple.setOS(llvm::Triple::NaCl);
        triple.setEnvironment(llvm::Triple::GNU);
        #else
        user_error << "This version of Halide was compiled without nacl support.\n";
        #endif
    } else if (target.os == Target::IOS) {
        user_error << "Can't use IOS on x86.\n";
    }

    return triple;
}

namespace {

// All sources of new types should get legalized. All sinks of types
// should get reverted to the original.

// Sources: Loads. Broadcast. Variables/Lets. Call results.
// Sinks: Store indices and values. Load indices. Call arguments.

/*
class LegalizeTypes : public IRMutator {
    Scope<Type> legalized_vars;

    using IRMutator::visit;

public:
    using IRMutator::mutate;
    Expr mutate(Expr e) {
        debug(0) << "Mutating " << e << "\n";
        Expr new_e = IRMutator::mutate(e);
        debug(0) << e << " -> " << new_e << "\n";
        return new_e;
    }
private:

    Expr mutate_and_delegalize(Expr e) {
        Expr new_e = mutate(e);
        if (new_e.type() != e.type()) {
            // Strip off the extra lanes
            vector<Expr> args;
            args.push_back(new_e);
            for (int i = 0; i < e.type().width; i++) {
                args.push_back(i);
            }
            return Call::make(e.type(), Call::shuffle_vector, args, Call::Intrinsic);
        } else {
            return new_e;
        }
    }

    Expr legalize(Expr e) {
        // bit-width of a vector type should be a multiple of 128
        Type t = e.type();
        if (t.is_vector()) {
            internal_assert(128 % t.bits == 0);
            int good_lanes = 128 / t.bits;
            int extra_lanes = 0;
            while ((t.width + extra_lanes) % good_lanes != 0) {
                extra_lanes++;
            }
            if (extra_lanes) {
                Type new_type = t;
                new_type.width += extra_lanes;
                vector<Expr> args;
                args.push_back(e);
                // The original elements
                for (int i = 0; i < t.width; i++) {
                    args.push_back(i);
                }
                // extra_lanes worth of undef values
                for (int i = 0; i < extra_lanes; i++) {
                    args.push_back(t.width);
                }
                return Call::make(new_type, Call::shuffle_vector, args, Call::Intrinsic);
            }
        }

        // Other legalizations here...

        return e;
    }

    void visit(const Broadcast *op) {
        IRMutator::visit(op);
        expr = legalize(expr);
    }

    void visit(const Cast *op) {
        Expr value = mutate_and_delegalize(op->value);
        if (!value.same_as(op->value)) {
            expr = Cast::make(op->type, value);
        } else {
            expr = op;
        }
        expr = legalize(expr);
    }

    void visit(const Variable *op) {
        if (legalized_vars.contains(op->name)) {
            expr = Variable::make(legalized_vars.get(op->name), op->name);
        } else {
            expr = legalize(op);
        }
    }

    template<typename Op, typename StmtOrExpr> StmtOrExpr visit_let(const Op *op) {

        Expr value = mutate(op->value);

        if (!value.same_as(op->value)) {
            legalized_vars.push(op->name, value.type());
        }

        StmtOrExpr body = mutate(op->body);

        if (!value.same_as(op->value)) {
            legalized_vars.pop(op->name);
        }

        if (value.same_as(op->value) && body.same_as(op->body)) {
            return op;
        } else {
            return Op::make(op->name, value, body);
        }
    }

    void visit(const LetStmt *op) {
        stmt = visit_let<LetStmt, Stmt>(op);
    }

    void visit(const Let *op) {
        expr = visit_let<Let, Expr>(op);
    }

    void visit(const Call *op) {
        vector<Expr> new_args(op->args.size());
        bool changed = false;
        for (size_t i = 0; i < op->args.size(); i++) {
            new_args[i] = mutate_and_delegalize(op->args[i]);
            if (!new_args[i].same_as(op->args[i])) {
                changed = true;
            }
        }

        if (changed) {
            expr = Call::make(op->type, op->name, new_args, op->call_type,
                              op->func, op->value_index, op->image, op->param);
        } else {
            expr = op;
        }

        expr = legalize(expr);
    }

    void visit(const Store *op) {
        Expr value = mutate_and_delegalize(op->value);
        Expr index = mutate_and_delegalize(op->index);

        if (value.same_as(op->value) && index.same_as(op->index)) {
            stmt = op;
        } else {
            stmt = Store::make(op->name, value, index);
        }
    }

    void visit(const Load *op) {
        Expr index = mutate_and_delegalize(op->index);

        if (index.same_as(op->index)) {
            expr = op;
        } else {
            expr = Load::make(op->type, op->name, index, op->image, op->param);
        }

        expr = legalize(expr);
    }

    void visit(const Select *op) {
        Expr condition = mutate(op->condition);
        Expr true_value = mutate(op->true_value);
        Expr false_value = mutate(op->false_value);
    }
};
*/

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

Value *CodeGen_X86::call_intrin(Type result_type, int vector_width, const string &name, vector<Expr> args) {
    vector<Value *> arg_values(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        arg_values[i] = codegen(args[i]);
    }

    return call_intrin(llvm_type_of(result_type), vector_width, name, arg_values);
}

Value *CodeGen_X86::call_intrin(llvm::Type *result_type, int vector_width, const string &name, vector<Value *> arg_values) {
    int result_vector_width = (int)result_type->getVectorNumElements();
    if (vector_width != result_vector_width) {
        // Cut up each arg into appropriately-sized pieces, call the
        // intrinsic on each, then splice together the results.
        vector<Value *> results;
        for (int start = 0; start < result_vector_width; start += vector_width) {
            vector<Value *> args;
            for (size_t i = 0; i < arg_values.size(); i++) {
                args.push_back(slice_vector(arg_values[i], start, vector_width));
            }

            llvm::Type *result_slice_type =
                llvm::VectorType::get(result_type->getScalarType(), vector_width);

            results.push_back(call_intrin(result_slice_type, vector_width, name, args));
        }
        Value *result = concat_vectors(results);
        return slice_vector(result, 0, result_vector_width);
    }

    vector<llvm::Type *> arg_types(arg_values.size());
    for (size_t i = 0; i < arg_values.size(); i++) {
        arg_types[i] = arg_values[i]->getType();
    }

    llvm::Function *fn = module->getFunction(name);

    if (!fn) {
        llvm::Type *intrinsic_result_type = VectorType::get(result_type->getScalarType(), vector_width);
        FunctionType *func_t = FunctionType::get(intrinsic_result_type, arg_types, false);
        fn = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, name, module);
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
    if (t == e.type()) {
        return e;
    } else if (t.can_represent(e.type())) {
        return cast(t, e);
    }

    if (const Cast *c = e.as<Cast>()) {
        if (t == c->value.type()) {
            return c->value;
        } else {
            return lossless_cast(t, c->value);
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
            return cast(t, e);
        } else {
            return Expr();
        }
    }

    return Expr();
}

// i32(i16_a)*i32(i16_b) +/- i32(i16_c)*i32(i16_d) can be done by
// interleaving a, c, and b, d, and then using pmaddwd. We
// recognize it here, and implement it in the initial module.
bool should_use_pmaddwd(Expr a, Expr b, vector<Expr> &result) {
    Type t = a.type();
    internal_assert(b.type() == t);

    const Mul *ma = a.as<Mul>();
    const Mul *mb = b.as<Mul>();

    if (!(ma && mb && t.is_int() &&
          t.bits == 32 && (t.width == 4 || t.width == 8))) {
        return false;
    }

    Type narrow = t;
    narrow.bits = 16;
    vector<Expr> args = vec(lossless_cast(narrow, ma->a),
                            lossless_cast(narrow, ma->b),
                            lossless_cast(narrow, mb->a),
                            lossless_cast(narrow, mb->b));
    if (!args[0].defined() || !args[1].defined() ||
        !args[2].defined() || !args[3].defined()) {
        return false;
    }

    result.swap(args);
    return true;
}

}


void CodeGen_X86::visit(const Add *op) {
    vector<Expr> matches;
    if (should_use_pmaddwd(op->a, op->b, matches)) {
        codegen(Call::make(op->type, "pmaddwd", matches, Call::Extern));
    } else {
        CodeGen::visit(op);
    }
}


void CodeGen_X86::visit(const Sub *op) {
    vector<Expr> matches;
    if (should_use_pmaddwd(op->a, op->b, matches)) {
        // Negate one of the factors in the second expression
        if (is_const(matches[2])) {
            matches[2] = -matches[2];
        } else {
            matches[3] = -matches[3];
        }
        codegen(Call::make(op->type, "pmaddwd", matches, Call::Extern));
    } else {
        CodeGen::visit(op);
    }
}

void CodeGen_X86::visit(const GT *op) {
    Type t = op->a.type();
    int bits = t.width * t.bits;
    if (t.width == 1 || bits % 128 == 0) {
        // LLVM is fine for native vector widths or scalars
        CodeGen::visit(op);
    } else {
        // Non-native vector widths get legalized poorly by llvm. We
        // split it up ourselves.
        Value *a = codegen(op->a), *b = codegen(op->b);

        int slice_size = 128 / t.bits;

        vector<Value *> result;
        for (int i = 0; i < op->type.width; i += slice_size) {
            Value *sa = slice_vector(a, i, slice_size);
            Value *sb = slice_vector(b, i, slice_size);
            Value *slice_value;
            if (t.is_float()) {
                slice_value = builder->CreateFCmpOGT(sa, sb);
            } else if (t.is_int()) {
                slice_value = builder->CreateICmpSGT(sa, sb);
            } else {
                slice_value = builder->CreateICmpUGT(sa, sb);
            }
            result.push_back(slice_value);
        }

        value = concat_vectors(result);
        value = slice_vector(value, 0, t.width);
    }
}

void CodeGen_X86::visit(const EQ *op) {
    Type t = op->a.type();
    int bits = t.width * t.bits;
    if (t.width == 1 || bits % 128 == 0) {
        // LLVM is fine for native vector widths or scalars
        CodeGen::visit(op);
    } else {
        // Non-native vector widths get legalized poorly by llvm. We
        // split it up ourselves.
        Value *a = codegen(op->a), *b = codegen(op->b);

        int slice_size = 128 / t.bits;

        vector<Value *> result;
        for (int i = 0; i < op->type.width; i += slice_size) {
            Value *sa = slice_vector(a, i, slice_size);
            Value *sb = slice_vector(b, i, slice_size);
            Value *slice_value;
            if (t.is_float()) {
                slice_value = builder->CreateFCmpOEQ(sa, sb);
            } else {
                slice_value = builder->CreateICmpEQ(sa, sb);
            }
            result.push_back(slice_value);
        }

        value = concat_vectors(result);
        value = slice_vector(value, 0, t.width);
    }
}

void CodeGen_X86::visit(const LT *op) {
    codegen(op->b > op->a);
}

void CodeGen_X86::visit(const LE *op) {
    codegen(!(op->a > op->b));
}

void CodeGen_X86::visit(const GE *op) {
    codegen(!(op->b > op->a));
}

void CodeGen_X86::visit(const NE *op) {
    codegen(!(op->a == op->b));
}

void CodeGen_X86::visit(const Cast *op) {

    if (!op->type.is_vector()) {
        // We only have peephole optimizations for vectors in here.
        CodeGen::visit(op);
        return;
    }

    vector<Expr> matches;

    struct Pattern {
        bool needs_sse_41;
        bool wide_op;
        Type type;
        string intrin;
        Expr pattern;
    };

    static Pattern patterns[] = {
        {false, true, Int(8, 16), "llvm.x86.sse2.padds.b",
         _i8(clamp(wild_i16x0 + wild_i16x0, -128, 127))},
        {false, true, Int(8, 16), "llvm.x86.sse2.psubs.b",
         _i8(clamp(wild_i16x0 - wild_i16x0, -128, 127))},
        {false, true, UInt(8, 16), "llvm.x86.sse2.paddus.b",
         _u8(min(wild_u16x0 + wild_u16x0, 255))},
        {false, true, UInt(8, 16), "llvm.x86.sse2.psubus.b",
         _u8(max(wild_i16x0 - wild_i16x0, 0))},
        {false, true, Int(16, 8), "llvm.x86.sse2.padds.w",
         _i16(clamp(wild_i32x0 + wild_i32x0, -32768, 32767))},
        {false, true, Int(16, 8), "llvm.x86.sse2.psubs.w",
         _i16(clamp(wild_i32x0 - wild_i32x0, -32768, 32767))},
        {false, true, UInt(16, 8), "llvm.x86.sse2.paddus.w",
         _u16(min(wild_u32x0 + wild_u32x0, 65535))},
        {false, true, UInt(16, 8), "llvm.x86.sse2.psubus.w",
         _u16(max(wild_i32x0 - wild_i32x0, 0))},
        {false, true, Int(16, 8), "llvm.x86.sse2.pmulh.w",
         _i16((wild_i32x0 * wild_i32x0) / 65536)},
        {false, true, UInt(16, 8), "llvm.x86.sse2.pmulhu.w",
         _u16((wild_u32x0 * wild_u32x0) / 65536)},
        {false, true, UInt(8, 16), "llvm.x86.sse2.pavg.b",
         _u8(((wild_u16x0 + wild_u16x0) + 1) / 2)},
        {false, true, UInt(16, 8), "llvm.x86.sse2.pavg.w",
         _u16(((wild_u32x0 + wild_u32x0) + 1) / 2)},
        {false, false, Int(16, 8), "packssdwx8",
         _i16(clamp(wild_i32x0, -32768, 32767))},
        {false, false, Int(8, 16), "packsswbx16",
         _i8(clamp(wild_i16x0, -128, 127))},
        {false, false, UInt(8, 16), "packuswbx16",
         _u8(clamp(wild_i16x0, 0, 255))},
        {true, false, UInt(16, 8), "packusdwx8",
         _u16(clamp(wild_i32x0, 0, 65535))}
    };

    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        const Pattern &pattern = patterns[i];

        if (!target.has_feature(Target::SSE41) && pattern.needs_sse_41) {
            continue;
        }

        if (expr_match(pattern.pattern, op, matches)) {
            bool match = true;
            if (pattern.wide_op) {
                // Try to narrow the matches to the target type.
                for (size_t i = 0; i < matches.size(); i++) {
                    matches[i] = lossless_cast(op->type, matches[i]);
                    if (!matches[i].defined()) match = false;
                }
            }
            if (match) {
                value = call_intrin(op->type, pattern.type.width, pattern.intrin, matches);
                return;
            }
        }
    }

    CodeGen::visit(op);

}

void CodeGen_X86::visit(const Div *op) {

    user_assert(!is_zero(op->b)) << "Division by constant zero in expression: " << Expr(op) << "\n";

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
    if (power_of_two && op->type.is_int()) {
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
        if (op->type.element_of() == Int(16) && op->type.is_vector()) {
            val = call_intrin(narrower, 8, "llvm.x86.sse2.pmulhu.w", vec(flipped, mult));
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

        internal_assert(method != 0)
            << "method 0 division is for powers of two and should have been handled elsewhere\n";

        Value *num = codegen(op->a);

        // Widen, multiply, narrow
        llvm::Type *narrower = llvm_type_of(op->type);
        llvm::Type *wider = llvm_type_of(UInt(op->type.bits*2, op->type.width));

        Value *mult = ConstantInt::get(narrower, multiplier);
        Value *val = num;

        if (op->type.element_of() == UInt(16) && op->type.is_vector()) {
            val = call_intrin(narrower, 8, "llvm.x86.sse2.pmulhu.w", vec(val, mult));
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
    if (!op->type.is_vector()) {
        CodeGen::visit(op);
        return;
    }

    bool use_sse_41 = target.has_feature(Target::SSE41);
    if (op->type.element_of() == UInt(8)) {
        value = call_intrin(op->type, 16, "llvm.x86.sse2.pminu.b", vec(op->a, op->b));
    } else if (use_sse_41 && op->type.element_of() == Int(8)) {
        value = call_intrin(op->type, 16, "llvm.x86.sse41.pminsb", vec(op->a, op->b));
    } else if (op->type.element_of() == Int(16)) {
        value = call_intrin(op->type, 8, "llvm.x86.sse2.pmins.w", vec(op->a, op->b));
    } else if (use_sse_41 && op->type.element_of() == UInt(16)) {
        value = call_intrin(op->type, 8, "llvm.x86.sse41.pminuw", vec(op->a, op->b));
    } else if (use_sse_41 && op->type.element_of() == Int(32)) {
        value = call_intrin(op->type, 4, "llvm.x86.sse41.pminsd", vec(op->a, op->b));
    } else if (use_sse_41 && op->type.element_of() == UInt(32)) {
        value = call_intrin(op->type, 4, "llvm.x86.sse41.pminud", vec(op->a, op->b));
    } else if (op->type.element_of() == Float(32)) {
        if (op->type.width % 8 == 0 && target.has_feature(Target::AVX)) {
            // This condition should possibly be > 4, rather than a
            // multiple of 8, but shuffling in undefs seems to work
            // poorly with avx.
            value = call_intrin(op->type, 8, "min_f32x8", vec(op->a, op->b));
        } else {
            value = call_intrin(op->type, 4, "min_f32x4", vec(op->a, op->b));
        }
    } else {
        CodeGen::visit(op);
    }
}

void CodeGen_X86::visit(const Max *op) {
    if (!op->type.is_vector()) {
        CodeGen::visit(op);
        return;
    }

    bool use_sse_41 = target.has_feature(Target::SSE41);
    if (op->type.element_of() == UInt(8)) {
        value = call_intrin(op->type, 16, "llvm.x86.sse2.pmaxu.b", vec(op->a, op->b));
    } else if (use_sse_41 && op->type.element_of() == Int(8)) {
        value = call_intrin(op->type, 16, "llvm.x86.sse41.pmaxsb", vec(op->a, op->b));
    } else if (op->type.element_of() == Int(16)) {
        value = call_intrin(op->type, 8, "llvm.x86.sse2.pmaxs.w", vec(op->a, op->b));
    } else if (use_sse_41 && op->type.element_of() == UInt(16)) {
        value = call_intrin(op->type, 8, "llvm.x86.sse41.pmaxuw", vec(op->a, op->b));
    } else if (use_sse_41 && op->type.element_of() == Int(32)) {
        value = call_intrin(op->type, 4, "llvm.x86.sse41.pmaxsd", vec(op->a, op->b));
    } else if (use_sse_41 && op->type.element_of() == UInt(32)) {
        value = call_intrin(op->type, 4, "llvm.x86.sse41.pmaxud", vec(op->a, op->b));
    } else if (op->type.element_of() == Float(32)) {
        if (op->type.width % 8 == 0 && target.has_feature(Target::AVX)) {
            value = call_intrin(op->type, 8, "max_f32x8", vec(op->a, op->b));
        } else {
            value = call_intrin(op->type, 4, "max_f32x4", vec(op->a, op->b));
        }
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
    loop = Allocate::make("tmp_stack", Int(32), vec(Expr(127)), const_true(), Block::make(loop, Free::make("tmp_stack")));
    loop = Allocate::make("tmp_heap", Int(32), vec(Expr(43), Expr(beta)), const_true(), Block::make(loop, Free::make("tmp_heap")));
    loop = For::make("i", -1, 3, For::Parallel, loop);

    Stmt s = Block::make(init, loop);

    CodeGen_X86 cg(get_host_target());
    cg.compile(s, "test1", args, vector<Buffer>());

    //cg.compile_to_bitcode("test1.bc");
    //cg.compile_to_native("test1.o", false);
    //cg.compile_to_native("test1.s", true);

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
    internal_assert(scratch[0] == 5);
    internal_assert(scratch[1] == 5);
    internal_assert(scratch[2] == 5);
    internal_assert(scratch[3] == 3);
    internal_assert(scratch[4] == 4*17);
    internal_assert(scratch[5] == 5);
    internal_assert(scratch[6] == 6*17);

    fn(&buf, 37.32f, 2);
    internal_assert(scratch[0] == 0);
    internal_assert(scratch[1] == 1);
    internal_assert(scratch[2] == 4);
    internal_assert(scratch[3] == 4);
    internal_assert(scratch[4] == 4);
    internal_assert(scratch[5] == 5);
    internal_assert(scratch[6] == 6*17);

    fn(&buf, 4.0f, 1);
    internal_assert(scratch[0] == 0);
    internal_assert(scratch[1] == 3);
    internal_assert(scratch[2] == 3);
    internal_assert(scratch[3] == 3);
    internal_assert(scratch[4] == 4*17);
    internal_assert(scratch[5] == 5);
    internal_assert(scratch[6] == 6*17);
    internal_assert(extern_function_1_was_called);

    // Check the wrapped version does the same thing
    extern_function_1_was_called = false;
    for (int i = 0; i < 16; i++) scratch[i] = 0;

    float float_arg_val = 4.0f;
    int int_arg_val = 1;
    const void *arg_array[] = {&buf, &float_arg_val, &int_arg_val};
    m.wrapped_function(arg_array);
    internal_assert(scratch[0] == 0);
    internal_assert(scratch[1] == 3);
    internal_assert(scratch[2] == 3);
    internal_assert(scratch[3] == 3);
    internal_assert(scratch[4] == 4*17);
    internal_assert(scratch[5] == 5);
    internal_assert(scratch[6] == 6*17);
    internal_assert(extern_function_1_was_called);

    std::cout << "CodeGen_X86 test passed" << std::endl;
}

string CodeGen_X86::mcpu() const {
    if (target.has_feature(Target::AVX)) return "corei7-avx";
    // We want SSE4.1 but not SSE4.2, hence "penryn" rather than "corei7"
    if (target.has_feature(Target::SSE41)) return "penryn";
    // Default should not include SSSE3, hence "k8" rather than "core2"
    return "k8";
}

string CodeGen_X86::mattrs() const {
    std::string features;
    std::string separator;
    #if LLVM_VERSION >= 35
    // These attrs only exist in llvm 3.5+
    if (target.has_feature(Target::FMA)) {
        features += "+fma";
        separator = " ";
    }
    if (target.has_feature(Target::FMA4)) {
        features += separator + "+fma4";
        separator = " ";
    }
    if (target.has_feature(Target::F16C)) {
        features += separator + "+f16c";
        separator = " ";
    }
    #endif
    return features;
}

bool CodeGen_X86::use_soft_float_abi() const {
    return false;
}

int CodeGen_X86::native_vector_bits() const {
    if (target.has_feature(Target::AVX)) {
        return 256;
    } else {
        return 128;
    }
}

void CodeGen_X86::jit_init(llvm::ExecutionEngine *ee, llvm::Module *)
{
    jitEventListener = llvm::JITEventListener::createIntelJITEventListener();
    if (jitEventListener) {
        ee->RegisterJITEventListener(jitEventListener);
    }
}

void CodeGen_X86::jit_finalize(llvm::ExecutionEngine * ee, llvm::Module *, std::vector<JITCompiledModule::CleanupRoutine> *)
{
    if (jitEventListener) {
        ee->UnregisterJITEventListener(jitEventListener);
        delete jitEventListener;
        jitEventListener = NULL;
    }
}

}}
