#include <iostream>

#include "CodeGen_X86.h"
#include "JITModule.h"
#include "IROperator.h"
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

CodeGen_X86::CodeGen_X86(Target t) : CodeGen_Posix(t) {

    #if !(WITH_X86)
    user_error << "x86 not enabled for this build of Halide.\n";
    #endif

    user_assert(llvm_X86_enabled) << "llvm build not configured with X86 target enabled.\n";

    #if !(WITH_NATIVE_CLIENT)
    user_assert(t.os != Target::NaCl) << "llvm build not configured with native client enabled.\n";
    #endif
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

    if (!(ma && mb && t.is_int() && t.bits == 32 && (t.width >= 4))) {
        return false;
    }

    Type narrow = t;
    narrow.bits = 16;
    vector<Expr> args = {lossless_cast(narrow, ma->a),
                         lossless_cast(narrow, ma->b),
                         lossless_cast(narrow, mb->a),
                         lossless_cast(narrow, mb->b)};
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
        CodeGen_Posix::visit(op);
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
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_X86::visit(const GT *op) {
    Type t = op->a.type();
    int bits = t.width * t.bits;
    if (t.width == 1 || bits % 128 == 0) {
        // LLVM is fine for native vector widths or scalars
        CodeGen_Posix::visit(op);
    } else {
        // Non-native vector widths get legalized poorly by llvm. We
        // split it up ourselves.
        Value *a = codegen(op->a), *b = codegen(op->b);

        int slice_size = 128 / t.bits;
        if (target.has_feature(Target::AVX) && bits > 128) {
            slice_size = 256 / t.bits;
        }

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
        CodeGen_Posix::visit(op);
    } else {
        // Non-native vector widths get legalized poorly by llvm. We
        // split it up ourselves.
        Value *a = codegen(op->a), *b = codegen(op->b);

        int slice_size = 128 / t.bits;
        if (target.has_feature(Target::AVX) && bits > 128) {
            slice_size = 256 / t.bits;
        }

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

void CodeGen_X86::visit(const Select *op) {

    // LLVM doesn't correctly use pblendvb for u8 vectors that aren't
    // width 16, so we peephole optimize them to intrinsics.
    struct Pattern {
        string intrin;
        Expr pattern;
    };

    static Pattern patterns[] = {
        {"pblendvb_ult_i8x16", select(wild_u8x_ < wild_u8x_, wild_i8x_, wild_i8x_)},
        {"pblendvb_ult_i8x16", select(wild_u8x_ < wild_u8x_, wild_u8x_, wild_u8x_)},
        {"pblendvb_slt_i8x16", select(wild_i8x_ < wild_i8x_, wild_i8x_, wild_i8x_)},
        {"pblendvb_slt_i8x16", select(wild_i8x_ < wild_i8x_, wild_u8x_, wild_u8x_)},
        {"pblendvb_ule_i8x16", select(wild_u8x_ <= wild_u8x_, wild_i8x_, wild_i8x_)},
        {"pblendvb_ule_i8x16", select(wild_u8x_ <= wild_u8x_, wild_u8x_, wild_u8x_)},
        {"pblendvb_sle_i8x16", select(wild_i8x_ <= wild_i8x_, wild_i8x_, wild_i8x_)},
        {"pblendvb_sle_i8x16", select(wild_i8x_ <= wild_i8x_, wild_u8x_, wild_u8x_)},
        {"pblendvb_ne_i8x16", select(wild_u8x_ != wild_u8x_, wild_i8x_, wild_i8x_)},
        {"pblendvb_ne_i8x16", select(wild_u8x_ != wild_u8x_, wild_u8x_, wild_u8x_)},
        {"pblendvb_ne_i8x16", select(wild_i8x_ != wild_i8x_, wild_i8x_, wild_i8x_)},
        {"pblendvb_ne_i8x16", select(wild_i8x_ != wild_i8x_, wild_i8x_, wild_i8x_)},
        {"pblendvb_eq_i8x16", select(wild_u8x_ == wild_u8x_, wild_i8x_, wild_i8x_)},
        {"pblendvb_eq_i8x16", select(wild_u8x_ == wild_u8x_, wild_u8x_, wild_u8x_)},
        {"pblendvb_eq_i8x16", select(wild_i8x_ == wild_i8x_, wild_i8x_, wild_i8x_)},
        {"pblendvb_eq_i8x16", select(wild_i8x_ == wild_i8x_, wild_i8x_, wild_i8x_)},
        {"pblendvb_i8x16", select(wild_u1x_, wild_i8x_, wild_i8x_)},
        {"pblendvb_i8x16", select(wild_u1x_, wild_u8x_, wild_u8x_)}
    };


    if (target.has_feature(Target::SSE41) &&
        op->condition.type().is_vector() &&
        op->type.bits == 8 &&
        op->type.width != 16) {

        vector<Expr> matches;
        for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
            if (expr_match(patterns[i].pattern, op, matches)) {
                value = call_intrin(op->type, 16, patterns[i].intrin, matches);
                return;
            }
        }
    }

    CodeGen_Posix::visit(op);

}

bool CodeGen_X86::try_visit_float16_cast(const Cast* op) {
    Type destTy = op->type;
    Type srcTy = op->value.type();

    // half -> single/double
    if (destTy.is_float() && srcTy.is_float() && srcTy.bits == 16 && destTy.bits > 16) {
        internal_assert(destTy.bits == 64 || destTy.bits == 32) << "Unexpected float type\n";

        if (target_needs_software_cast_from_float16_to(destTy)) {
            // Let parent class codegen calling software implementation of cast
            return false;
        }

        // Use @llvm.x86.vcvtph2ps.128 or @llvm.x86.vcvtph2ps.256 to handle the
        // conversion
        internal_assert(srcTy.width == destTy.width) << "Source and destination widths must match\n";
        if (srcTy.width <= 4) {
            // Use @llvm.x86.vcvtph2ps.128. This returns <4 x float>
            // FIXME: Codegen using other widths (e.g. 3) seems to give wrong
            // results explicitly disable until this can be investigated.
            internal_assert(srcTy.width == 4 || srcTy.width == 1) <<
              "FIXME: Doing codegen with width less than four produces incorrect results\n";

            // Codegen argument
            llvm::Value* valueToCast = codegen(op->value);

            // Note with width is 8 here because that is what is expected by
            // the instrinsic. Only the first 4 lanes matter here.
            // Slice to correct width
            if (srcTy.width == 1) {
                // Handle scalar value by inserting as the first element
                // in a vector
                internal_assert(!(valueToCast->getType()->isVectorTy()));
                Constant* zeroHalfVectorSplat = ConstantFP::get(f16x8, 0.0);
                valueToCast = builder->CreateInsertElement(/*Vec=*/zeroHalfVectorSplat,
                                                           /*NewElt=*/valueToCast,
                                                           /*Idx=*/ConstantInt::get(i32, 0)
                                                          );
            } else {
                // Handle vectorized inputs by extending to correct width.
                // Introduced undef lanes will be removed later.
                valueToCast = slice_vector(valueToCast, /*start=*/0, /*size=*/8);
            }

            // Cast argument to <8 x i16> as expected by the intrinsic
            valueToCast = builder->CreateBitCast(valueToCast, i16x8);

            // Can't use call_intrin() because it asserts the number of elements
            // in the source and dest match so call it manually.
            llvm::Function* fn = module->getFunction("llvm.x86.vcvtph2ps.128");
            internal_assert(fn != nullptr) << "Could not find intrinsic\n";

            llvm::Value* result = builder->CreateCall(fn, { valueToCast });

            if (destTy.bits == 64) {
                // Cast the single to double
                result = builder->CreateFPExt(result, f64x4);
            }

            if (srcTy.width == 1) {
                // Handling a scalar value so extract the element we want
                result = builder->CreateExtractElement(/*Vec=*/result,
                                                       /*Idx=*/ConstantInt::get(i32, 0)
                                                      );
            } else {
                // Remove undef lanes that might have been introduced by first
                // call to slice_vector()
                result = slice_vector(result, /*start=*/0, /*size=*/destTy.width);
            }

            value = result;
        } else {
            // Use @llvm.x86.vcvtph2ps.256 this results <8x float>
            // The number of elements in the argument and result match
            // so we can use call_intrin() here.

            // Codegen the argument
            llvm::Value* valueToCast = codegen(op->value);
            // Bitcast the argument to the right type
            // e.g. <8 x half> ==> <8 x i16>
            int numElements = 0;
            if (llvm::VectorType* vecTy = dyn_cast<VectorType>(valueToCast->getType())) {
                numElements = vecTy->getNumElements();
            } else {
                internal_error << "Expecting vector type\n";
            }
            // FIXME: Leak?
            llvm::Type* newVectorType = VectorType::get(i16, numElements);
            valueToCast = builder->CreateBitCast(valueToCast, newVectorType);


            // FIXME: destTy isn't really the return type when working with
            // doubles but this doesn't matter as long as the intrinsic is
            // already declared in x86.ll
            llvm::Value* result = call_intrin(/*result_type=*/llvm_type_of(destTy),
                                              /*intrin_vector_width=*/8,
                                              /*name=*/"llvm.x86.vcvtph2ps.256",
                                              /*arg_values=*/{ valueToCast }
                                             );

            if (destTy.bits == 64) {
                // Cast the single to double. It's width is not necessarily a
                // fp64x8 so we need to construct a new type representing a
                // vector of the right number of doubles
                // FIXME: Leak?
                llvm::Type* newDblVectorType = VectorType::get(f64, destTy.width);
                result = builder->CreateFPExt(result, newDblVectorType);
            }

            value = result;
        }
        return true;
    }

    // single -> half
    // There is no native "double -> half" support. The software implementation
    // will be used for this
    if (destTy.is_float() && srcTy.is_float() && srcTy.bits == 32 && destTy.bits == 16) {
        internal_assert(srcTy.width == destTy.width) << "Source and destination widths must match\n";
        if (target_needs_software_cast_to_float16_from(srcTy, op->roundingMode)) {
            // Let parent class codegen calling software implementation of cast
            return false;
        }

        // Set the rounding arg mode argument that will be passed to the
        // vcvtps2ph intrinsic.
        // See "Table 4-17 Immediate Byte Encoding for 16-bit Floating-Point
        // Conversion Instructions" from "Intel 64 and IA-32 Architectures
        // Software Developer’s Manual"
        Constant* roundingModeArg = nullptr;
        switch (op->roundingMode) {
            case RoundingMode::TowardZero:
                roundingModeArg = ConstantInt::get(i32, 3, /*isSigned=*/false);
                break;
            case RoundingMode::ToNearestTiesToEven:
                roundingModeArg = ConstantInt::get(i32, 0, /*isSigned=*/false);
                break;
            case RoundingMode::TowardPositiveInfinity:
                roundingModeArg = ConstantInt::get(i32, 2, /*isSigned=*/false);
                break;
            case RoundingMode::TowardNegativeInfinity:
                roundingModeArg = ConstantInt::get(i32, 1, /*isSigned=*/false);
                break;
            default:
                internal_error << "Unsupported rounding mode\n";
        }
        internal_assert(roundingModeArg != nullptr);

        // Codegen argument
        llvm::Value* valueToCast = codegen(op->value);

        // Use @llvm.x86.vcvtps2ph.128 or @llvm.x86.vcvtps2ph.256 to handle the
        // conversion
        if (srcTy.width <= 4) {
            // Use
            // <8 x i16> @llvm.x86.vcvtps2ph.128(<4 x float> %a, i32 %roundingMode) readnone
            // Note that only the first 4 lanes in the returned <8 xi16> are
            // relevant so we'll need to slice the vector afterwards

            if (srcTy.width == 1) {
                // Handle scalar value by inserting as the first element in a
                // vector
                Constant* zeroFloatVectorSplat = ConstantFP::get(f32x4, 0.0);
                valueToCast = builder->CreateInsertElement(/*Vec=*/zeroFloatVectorSplat,
                                                           /*NewElt=*/valueToCast,
                                                           /*Idx=*/ConstantInt::get(i32, 0)
                                                          );
            } else {
                // Handle vectorized inputs by extending to correct width.
                // Introduced undef lanes will be removed later
                valueToCast = slice_vector(valueToCast, /*start=*/0, /*size=*/4);
            }

            // Can't use call_intrin() because it asserts the number of elements
            // in the source and dest match so call it manually.
            llvm::Function* fn = module->getFunction("llvm.x86.vcvtps2ph.128");
            internal_assert(fn != nullptr) << "Could not find intrinsic\n";

            llvm::Value* result = builder->CreateCall(fn, {valueToCast, roundingModeArg});

            // Bitcast the result to half <8 x i16> -> <8 x half>
            result = builder->CreateBitCast(result, f16x8);

            if (srcTy.width == 1) {
                // Handling scalar value to extract the element we want
                result = builder->CreateExtractElement(/*Vec=*/result,
                                                       /*Idx=*/ConstantInt::get(i32, 0)
                                                      );
            } else {
                // We get a <8 x i16> back. We don't need all these lanes so
                // slice out the lanes we want
                result = slice_vector(result, /*start=*/0, /*size=*/srcTy.width);
            }

            value=result;

        } else {
            // Use
            // <8 x i16> @llvm.x86.vcvtps2ph.256(<8 x float> %a, i32 %roundingMode) readnone
            // The number of elements in the input vector match the output so we
            // can use call_intrin() here

            // FIXME: Leak?
            llvm::Type* newVectorType = VectorType::get(i16, srcTy.width);

            llvm::Value* result = call_intrin(/*result_type=*/newVectorType,
                                             /*intrin_vector_width=*/8,
                                             /*name=*/"llvm.x86.vcvtps2ph.256",
                                             /*arg_values=*/{ valueToCast,
                                                              roundingModeArg}
                                            );

            // Cast <? x i16> to <? x half>
            int numElements = 0;
            if (llvm::VectorType* vecTy = dyn_cast<llvm::VectorType>(result->getType())) {
                numElements = vecTy->getNumElements();
            } else {
                    internal_error << "Not vector type\n";
            }
            // FIXME: Leak?
            llvm::Type* newHalfVectorType = VectorType::get(f16, numElements);
            result = builder->CreateBitCast(result, newHalfVectorType);
            value = result;
        }
        return true;
    }

    // Not a half conversion
    return false;
}

void CodeGen_X86::visit(const Cast *op) {
    if (try_visit_float16_cast(op)) {
        // Doing cast using a Float16 which has already been handled
        return;
    }


    if (!op->type.is_vector()) {
        // We only have peephole optimizations for vectors from this point on
        CodeGen_Posix::visit(op);
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
         _i8(clamp(wild_i16x_ + wild_i16x_, -128, 127))},
        {false, true, Int(8, 16), "llvm.x86.sse2.psubs.b",
         _i8(clamp(wild_i16x_ - wild_i16x_, -128, 127))},
        {false, true, UInt(8, 16), "llvm.x86.sse2.paddus.b",
         _u8(min(wild_u16x_ + wild_u16x_, 255))},
        {false, true, UInt(8, 16), "llvm.x86.sse2.psubus.b",
         _u8(max(wild_i16x_ - wild_i16x_, 0))},
        {false, true, Int(16, 8), "llvm.x86.sse2.padds.w",
         _i16(clamp(wild_i32x_ + wild_i32x_, -32768, 32767))},
        {false, true, Int(16, 8), "llvm.x86.sse2.psubs.w",
         _i16(clamp(wild_i32x_ - wild_i32x_, -32768, 32767))},
        {false, true, UInt(16, 8), "llvm.x86.sse2.paddus.w",
         _u16(min(wild_u32x_ + wild_u32x_, 65535))},
        {false, true, UInt(16, 8), "llvm.x86.sse2.psubus.w",
         _u16(max(wild_i32x_ - wild_i32x_, 0))},
        {false, true, Int(16, 8), "llvm.x86.sse2.pmulh.w",
         _i16((wild_i32x_ * wild_i32x_) / 65536)},
        {false, true, UInt(16, 8), "llvm.x86.sse2.pmulhu.w",
         _u16((wild_u32x_ * wild_u32x_) / 65536)},
        {false, true, UInt(8, 16), "llvm.x86.sse2.pavg.b",
         _u8(((wild_u16x_ + wild_u16x_) + 1) / 2)},
        {false, true, UInt(16, 8), "llvm.x86.sse2.pavg.w",
         _u16(((wild_u32x_ + wild_u32x_) + 1) / 2)},
        {false, false, Int(16, 8), "packssdwx8",
         _i16(clamp(wild_i32x_, -32768, 32767))},
        {false, false, Int(8, 16), "packsswbx16",
         _i8(clamp(wild_i16x_, -128, 127))},
        {false, false, UInt(8, 16), "packuswbx16",
         _u8(clamp(wild_i16x_, 0, 255))},
        {true, false, UInt(16, 8), "packusdwx8",
         _u16(clamp(wild_i32x_, 0, 65535))}
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


    #if LLVM_VERSION >= 38
    // Workaround for https://llvm.org/bugs/show_bug.cgi?id=24512
    // LLVM uses a numerically unstable method for vector
    // uint32->float conversion before AVX.
    if (op->value.type().element_of() == UInt(32) &&
        op->type.is_float() &&
        op->type.is_vector() &&
        !target.has_feature(Target::AVX)) {
        Type signed_type = Int(32, op->type.width);

        // Convert the top 31 bits to float using the signed version
        Expr top_bits = cast(signed_type, op->value / 2);
        top_bits = cast(op->type, top_bits);

        // Convert the bottom bit
        Expr bottom_bit = cast(signed_type, op->value % 2);
        bottom_bit = cast(op->type, bottom_bit);

        // Recombine as floats
        codegen(top_bits + top_bits + bottom_bit);
        return;
    }
    #endif


    CodeGen_Posix::visit(op);
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
    bool power_of_two = is_const_power_of_two_integer(op->b, &shift_amount);

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
            val = call_intrin(narrower, 8, "llvm.x86.sse2.pmulhu.w", {flipped, mult});
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
            val = call_intrin(narrower, 8, "llvm.x86.sse2.pmulhu.w", {val, mult});
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
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_X86::visit(const Min *op) {
    if (!op->type.is_vector()) {
        CodeGen_Posix::visit(op);
        return;
    }

    bool use_sse_41 = target.has_feature(Target::SSE41);
    if (op->type.element_of() == UInt(8)) {
        value = call_intrin(op->type, 16, "llvm.x86.sse2.pminu.b", {op->a, op->b});
    } else if (use_sse_41 && op->type.element_of() == Int(8)) {
        value = call_intrin(op->type, 16, "llvm.x86.sse41.pminsb", {op->a, op->b});
    } else if (op->type.element_of() == Int(16)) {
        value = call_intrin(op->type, 8, "llvm.x86.sse2.pmins.w", {op->a, op->b});
    } else if (use_sse_41 && op->type.element_of() == UInt(16)) {
        value = call_intrin(op->type, 8, "llvm.x86.sse41.pminuw", {op->a, op->b});
    } else if (use_sse_41 && op->type.element_of() == Int(32)) {
        value = call_intrin(op->type, 4, "llvm.x86.sse41.pminsd", {op->a, op->b});
    } else if (use_sse_41 && op->type.element_of() == UInt(32)) {
        value = call_intrin(op->type, 4, "llvm.x86.sse41.pminud", {op->a, op->b});
    } else if (op->type.element_of() == Float(32)) {
        if (op->type.width % 8 == 0 && target.has_feature(Target::AVX)) {
            // This condition should possibly be > 4, rather than a
            // multiple of 8, but shuffling in undefs seems to work
            // poorly with avx.
            value = call_intrin(op->type, 8, "min_f32x8", {op->a, op->b});
        } else {
            value = call_intrin(op->type, 4, "min_f32x4", {op->a, op->b});
        }
    } else if (op->type.element_of() == Float(64)) {
        if (op->type.width % 4 == 0 && target.has_feature(Target::AVX)) {
            value = call_intrin(op->type, 4, "min_f64x4", {op->a, op->b});
        } else {
            value = call_intrin(op->type, 2, "min_f64x2", {op->a, op->b});
        }
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_X86::visit(const Max *op) {
    if (!op->type.is_vector()) {
        CodeGen_Posix::visit(op);
        return;
    }

    bool use_sse_41 = target.has_feature(Target::SSE41);
    if (op->type.element_of() == UInt(8)) {
        value = call_intrin(op->type, 16, "llvm.x86.sse2.pmaxu.b", {op->a, op->b});
    } else if (use_sse_41 && op->type.element_of() == Int(8)) {
        value = call_intrin(op->type, 16, "llvm.x86.sse41.pmaxsb", {op->a, op->b});
    } else if (op->type.element_of() == Int(16)) {
        value = call_intrin(op->type, 8, "llvm.x86.sse2.pmaxs.w", {op->a, op->b});
    } else if (use_sse_41 && op->type.element_of() == UInt(16)) {
        value = call_intrin(op->type, 8, "llvm.x86.sse41.pmaxuw", {op->a, op->b});
    } else if (use_sse_41 && op->type.element_of() == Int(32)) {
        value = call_intrin(op->type, 4, "llvm.x86.sse41.pmaxsd", {op->a, op->b});
    } else if (use_sse_41 && op->type.element_of() == UInt(32)) {
        value = call_intrin(op->type, 4, "llvm.x86.sse41.pmaxud", {op->a, op->b});
    } else if (op->type.element_of() == Float(32)) {
        if (op->type.width % 8 == 0 && target.has_feature(Target::AVX)) {
            value = call_intrin(op->type, 8, "max_f32x8", {op->a, op->b});
        } else {
            value = call_intrin(op->type, 4, "max_f32x4", {op->a, op->b});
        }
    } else if (op->type.element_of() == Float(64)) {
        if (op->type.width % 4 == 0 && target.has_feature(Target::AVX)) {
            value = call_intrin(op->type, 4, "max_f64x4", {op->a, op->b});
        } else {
            value = call_intrin(op->type, 2, "max_f64x2", {op->a, op->b});
        }
    } else {
        CodeGen_Posix::visit(op);
    }
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
        separator = ",";
    }
    if (target.has_feature(Target::FMA4)) {
        features += separator + "+fma4";
        separator = ",";
    }
    if (target.has_feature(Target::F16C)) {
        features += separator + "+f16c";
        separator = ",";
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

bool CodeGen_X86::target_needs_software_cast_from_float16_to(Type t) const {
    internal_assert(t.bits == 32 || t.bits == 64);
    internal_assert(t.is_float());
    // float16 -> t
    internal_assert(t.is_float()) << "float16 -> t, where is not a float type not supported\n";
    if (target.has_feature(Target::F16C)) {
        // vcvtph2ps can handle float16 -> float
        // and float16 -> double can be handled by doing
        // a ``fpextend`` after using vcvtph2ps
        return false;
    }

    // Without F16C there is no hardware support so must use
    // software implementation
    return true;
}

bool CodeGen_X86::target_needs_software_cast_to_float16_from(Type t, RoundingMode rm) const {
    internal_assert(t.bits == 32 || t.bits == 64);
    internal_assert(t.is_float());
    internal_assert(rm != RoundingMode::Undefined) << "Rounding mode cannot be undefined\n";
    if (target.has_feature(Target::F16C)) {
        if (t.bits == 32) {
            // vcvtps2ph can handle float -> float16
            // provided the rounding mode is one support by the instruction
            if (rm == RoundingMode::ToNearestTiesToAway) {
                // vcvtps2ph doesn't support this rounding mode
                // so must use software implementation
                return true;
            }
            return false;
        } else {
          // double -> float16
          // No native support for this.
          // Doing ``double -> float -> float16`` is not safe.
          // so request software implementation which will only round once
          return true;
        }
    }

    // Without F16C there is no hardware support so must use
    // software implementation
    return true;
}

}}
