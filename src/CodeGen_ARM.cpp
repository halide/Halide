#include <set>
#include <sstream>

#include "CSE.h"
#include "CodeGen_Internal.h"
#include "CodeGen_Posix.h"
#include "ConciseCasts.h"
#include "Debug.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "LLVM_Headers.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

using namespace Halide::ConciseCasts;
using namespace llvm;

#if defined(WITH_ARM) || defined(WITH_AARCH64)

namespace {

/** A code generator that emits ARM code from a given Halide stmt. */
class CodeGen_ARM : public CodeGen_Posix {
public:
    /** Create an ARM code generator for the given arm target. */
    CodeGen_ARM(const Target &);

protected:
    using CodeGen_Posix::visit;

    /** Assuming 'inner' is a function that takes two vector arguments, define a wrapper that
     * takes one vector argument and splits it into two to call inner. */
    llvm::Function *define_concat_args_wrapper(llvm::Function *inner, const string &name);
    void init_module() override;

    /** Nodes for which we want to emit specific neon intrinsics */
    // @{
    void visit(const Cast *) override;
    void visit(const Sub *) override;
    void visit(const Min *) override;
    void visit(const Max *) override;
    void visit(const Store *) override;
    void visit(const Load *) override;
    void visit(const Call *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void codegen_vector_reduce(const VectorReduce *, const Expr &) override;
    // @}
    Type upgrade_type_for_arithmetic(const Type &t) const override;
    Type upgrade_type_for_argument_passing(const Type &t) const override;
    Type upgrade_type_for_storage(const Type &t) const override;

    /** Various patterns to peephole match against */
    struct Pattern {
        string intrin;  ///< Name of the intrinsic
        Expr pattern;   ///< The pattern to match against
        Pattern() = default;
        Pattern(const string &intrin, Expr p)
            : intrin(intrin), pattern(std::move(p)) {
        }
    };
    vector<Pattern> casts, calls, averagings, negations;

    string mcpu_target() const override;
    string mcpu_tune() const override;
    string mattrs() const override;
    bool use_soft_float_abi() const override;
    int native_vector_bits() const override;

    // NEON can be disabled for older processors.
    bool neon_intrinsics_disabled() {
        return target.has_feature(Target::NoNEON);
    }

    bool is_float16_and_has_feature(const Type &t) const {
        // NOTE : t.is_float() returns true even in case of BFloat16. We don't include it for now.
        return t.code() == Type::Float && t.bits() == 16 && target.has_feature(Target::ARMFp16);
    }
    bool supports_call_as_float16(const Call *op) const override;
};

CodeGen_ARM::CodeGen_ARM(const Target &target)
    : CodeGen_Posix(target) {

    // RADDHN - Add and narrow with rounding
    // These must come before other narrowing rounding shift patterns
    casts.emplace_back("rounding_add_narrow", i8(rounding_shift_right(wild_i16x_ + wild_i16x_, 8)));
    casts.emplace_back("rounding_add_narrow", u8(rounding_shift_right(wild_u16x_ + wild_u16x_, 8)));
    casts.emplace_back("rounding_add_narrow", i16(rounding_shift_right(wild_i32x_ + wild_i32x_, 16)));
    casts.emplace_back("rounding_add_narrow", u16(rounding_shift_right(wild_u32x_ + wild_u32x_, 16)));
    casts.emplace_back("rounding_add_narrow", i32(rounding_shift_right(wild_i64x_ + wild_i64x_, 32)));
    casts.emplace_back("rounding_add_narrow", u32(rounding_shift_right(wild_u64x_ + wild_u64x_, 32)));

    // RSUBHN - Add and narrow with rounding
    // These must come before other narrowing rounding shift patterns
    casts.emplace_back("rounding_sub_narrow", i8(rounding_shift_right(wild_i16x_ - wild_i16x_, 8)));
    casts.emplace_back("rounding_sub_narrow", u8(rounding_shift_right(wild_u16x_ - wild_u16x_, 8)));
    casts.emplace_back("rounding_sub_narrow", i16(rounding_shift_right(wild_i32x_ - wild_i32x_, 16)));
    casts.emplace_back("rounding_sub_narrow", u16(rounding_shift_right(wild_u32x_ - wild_u32x_, 16)));
    casts.emplace_back("rounding_sub_narrow", i32(rounding_shift_right(wild_i64x_ - wild_i64x_, 32)));
    casts.emplace_back("rounding_sub_narrow", u32(rounding_shift_right(wild_u64x_ - wild_u64x_, 32)));

    // QDMULH - Saturating doubling multiply keep high half
    calls.emplace_back("qdmulh", mul_shift_right(wild_i16x_, wild_i16x_, 15));
    calls.emplace_back("qdmulh", mul_shift_right(wild_i32x_, wild_i32x_, 31));

    // QRDMULH - Saturating doubling multiply keep high half with rounding
    calls.emplace_back("qrdmulh", rounding_mul_shift_right(wild_i16x_, wild_i16x_, 15));
    calls.emplace_back("qrdmulh", rounding_mul_shift_right(wild_i32x_, wild_i32x_, 31));

    // RSHRN - Rounding shift right narrow (by immediate in [1, output bits])
    casts.emplace_back("rounding_shift_right_narrow", i8(rounding_shift_right(wild_i16x_, wild_u16_)));
    casts.emplace_back("rounding_shift_right_narrow", u8(rounding_shift_right(wild_u16x_, wild_u16_)));
    casts.emplace_back("rounding_shift_right_narrow", u8(rounding_shift_right(wild_i16x_, wild_u16_)));
    casts.emplace_back("rounding_shift_right_narrow", i16(rounding_shift_right(wild_i32x_, wild_u32_)));
    casts.emplace_back("rounding_shift_right_narrow", u16(rounding_shift_right(wild_u32x_, wild_u32_)));
    casts.emplace_back("rounding_shift_right_narrow", u16(rounding_shift_right(wild_i32x_, wild_u32_)));
    casts.emplace_back("rounding_shift_right_narrow", i32(rounding_shift_right(wild_i64x_, wild_u64_)));
    casts.emplace_back("rounding_shift_right_narrow", u32(rounding_shift_right(wild_u64x_, wild_u64_)));
    casts.emplace_back("rounding_shift_right_narrow", u32(rounding_shift_right(wild_i64x_, wild_u64_)));

    // SHRN - Shift right narrow (by immediate in [1, output bits])
    casts.emplace_back("shift_right_narrow", i8(wild_i16x_ >> wild_u16_));
    casts.emplace_back("shift_right_narrow", u8(wild_u16x_ >> wild_u16_));
    casts.emplace_back("shift_right_narrow", i16(wild_i32x_ >> wild_u32_));
    casts.emplace_back("shift_right_narrow", u16(wild_u32x_ >> wild_u32_));
    casts.emplace_back("shift_right_narrow", i32(wild_i64x_ >> wild_u64_));
    casts.emplace_back("shift_right_narrow", u32(wild_u64x_ >> wild_u64_));

    // SQRSHL, UQRSHL - Saturating rounding shift left (by signed vector)
    // TODO: We need to match rounding shift right, and negate the RHS.

    // SQRSHRN, SQRSHRUN, UQRSHRN - Saturating rounding narrowing shift right narrow (by immediate in [1, output bits])
    calls.emplace_back("saturating_rounding_shift_right_narrow", i8_sat(rounding_shift_right(wild_i16x_, wild_u16_)));
    calls.emplace_back("saturating_rounding_shift_right_narrow", u8_sat(rounding_shift_right(wild_u16x_, wild_u16_)));
    calls.emplace_back("saturating_rounding_shift_right_narrow", u8_sat(rounding_shift_right(wild_i16x_, wild_u16_)));
    calls.emplace_back("saturating_rounding_shift_right_narrow", i16_sat(rounding_shift_right(wild_i32x_, wild_u32_)));
    calls.emplace_back("saturating_rounding_shift_right_narrow", u16_sat(rounding_shift_right(wild_u32x_, wild_u32_)));
    calls.emplace_back("saturating_rounding_shift_right_narrow", u16_sat(rounding_shift_right(wild_i32x_, wild_u32_)));
    calls.emplace_back("saturating_rounding_shift_right_narrow", i32_sat(rounding_shift_right(wild_i64x_, wild_u64_)));
    calls.emplace_back("saturating_rounding_shift_right_narrow", u32_sat(rounding_shift_right(wild_u64x_, wild_u64_)));
    calls.emplace_back("saturating_rounding_shift_right_narrow", u32_sat(rounding_shift_right(wild_i64x_, wild_u64_)));

    // SQSHL, UQSHL, SQSHLU - Saturating shift left by signed register.
    for (const Expr &rhs : {wild_i8x_, wild_u8x_}) {
        calls.emplace_back("saturating_shift_left", i8_sat(widening_shift_left(wild_i8x_, rhs)));
        calls.emplace_back("saturating_shift_left", u8_sat(widening_shift_left(wild_u8x_, rhs)));
        calls.emplace_back("saturating_shift_left", u8_sat(widening_shift_left(wild_i8x_, rhs)));
    }
    for (const Expr &rhs : {wild_i16x_, wild_u16x_}) {
        calls.emplace_back("saturating_shift_left", i16_sat(widening_shift_left(wild_i16x_, rhs)));
        calls.emplace_back("saturating_shift_left", u16_sat(widening_shift_left(wild_u16x_, rhs)));
        calls.emplace_back("saturating_shift_left", u16_sat(widening_shift_left(wild_i16x_, rhs)));
    }
    for (const Expr &rhs : {wild_i32x_, wild_u32x_}) {
        calls.emplace_back("saturating_shift_left", i32_sat(widening_shift_left(wild_i32x_, rhs)));
        calls.emplace_back("saturating_shift_left", u32_sat(widening_shift_left(wild_u32x_, rhs)));
        calls.emplace_back("saturating_shift_left", u32_sat(widening_shift_left(wild_i32x_, rhs)));
    }

    // SQSHRN, UQSHRN, SQRSHRUN Saturating narrowing shift right by an (by immediate in [1, output bits])
    calls.emplace_back("saturating_shift_right_narrow", i8_sat(wild_i16x_ >> wild_u16_));
    calls.emplace_back("saturating_shift_right_narrow", u8_sat(wild_u16x_ >> wild_u16_));
    calls.emplace_back("saturating_shift_right_narrow", u8_sat(wild_i16x_ >> wild_u16_));
    calls.emplace_back("saturating_shift_right_narrow", i16_sat(wild_i32x_ >> wild_u32_));
    calls.emplace_back("saturating_shift_right_narrow", u16_sat(wild_u32x_ >> wild_u32_));
    calls.emplace_back("saturating_shift_right_narrow", u16_sat(wild_i32x_ >> wild_u32_));
    calls.emplace_back("saturating_shift_right_narrow", i32_sat(wild_i64x_ >> wild_u64_));
    calls.emplace_back("saturating_shift_right_narrow", u32_sat(wild_u64x_ >> wild_u64_));
    calls.emplace_back("saturating_shift_right_narrow", u32_sat(wild_i64x_ >> wild_u64_));

    // SRSHL, URSHL - Rounding shift left (by signed vector)
    // These are already written as rounding_shift_left

    // SRSHR, URSHR - Rounding shift right (by immediate in [1, output bits])
    // These patterns are almost identity, we just need to strip off the broadcast.

    // SSHLL, USHLL - Shift left long (by immediate in [0, output bits - 1])
    // These patterns are almost identity, we just need to strip off the broadcast.

    // SQXTN, UQXTN, SQXTUN - Saturating narrow.
    calls.emplace_back("saturating_narrow", i8_sat(wild_i16x_));
    calls.emplace_back("saturating_narrow", u8_sat(wild_u16x_));
    calls.emplace_back("saturating_narrow", u8_sat(wild_i16x_));
    calls.emplace_back("saturating_narrow", i16_sat(wild_i32x_));
    calls.emplace_back("saturating_narrow", u16_sat(wild_u32x_));
    calls.emplace_back("saturating_narrow", u16_sat(wild_i32x_));
    calls.emplace_back("saturating_narrow", i32_sat(wild_i64x_));
    calls.emplace_back("saturating_narrow", u32_sat(wild_u64x_));
    calls.emplace_back("saturating_narrow", u32_sat(wild_i64x_));

    // SQNEG - Saturating negate
    negations.emplace_back("saturating_negate", -max(wild_i8x_, -127));
    negations.emplace_back("saturating_negate", -max(wild_i16x_, -32767));
    negations.emplace_back("saturating_negate", -max(wild_i32x_, -(0x7fffffff)));
    // clang-format on
}

constexpr int max_intrinsic_args = 4;

struct ArmIntrinsic {
    const char *arm32;
    const char *arm64;
    halide_type_t ret_type;
    const char *name;
    halide_type_t arg_types[max_intrinsic_args];
    int flags;
    enum {
        AllowUnsignedOp1 = 1 << 0,   // Generate a second version of the instruction with the second operand unsigned.
        HalfWidth = 1 << 1,          // This is a half-width instruction that should have a full width version generated as well.
        NoMangle = 1 << 2,           // Don't mangle this intrinsic name.
        MangleArgs = 1 << 3,         // Most intrinsics only mangle the return type. Some mangle the arguments instead.
        MangleRetArgs = 1 << 4,      // Most intrinsics only mangle the return type. Some mangle the return type and arguments instead.
        ScalarsAreVectors = 1 << 5,  // Some intrinsics have scalar arguments that are vector parameters :(
        SplitArg0 = 1 << 6,          // This intrinsic requires splitting the argument into the low and high halves.
        NoPrefix = 1 << 7,           // Don't prefix the intrinsic with llvm.*
        RequireFp16 = 1 << 8,        // Available only if Target has ARMFp16 feature
    };
};

// clang-format off
const ArmIntrinsic intrinsic_defs[] = {
    {"vabs", "abs", UInt(8, 8), "abs", {Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vabs", "abs", UInt(16, 4), "abs", {Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vabs", "abs", UInt(32, 2), "abs", {Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"llvm.fabs", "llvm.fabs", Float(32, 2), "abs", {Float(32, 2)}, ArmIntrinsic::HalfWidth},
    {"llvm.fabs", "llvm.fabs", Float(16, 4), "abs", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16},

    {"llvm.sqrt", "llvm.sqrt", Float(32, 2), "sqrt_f32", {Float(32, 2)}, ArmIntrinsic::HalfWidth},
    {"llvm.sqrt", "llvm.sqrt", Float(64, 2), "sqrt_f64", {Float(64, 2)}},

    {"llvm.roundeven", "llvm.roundeven", Float(16, 8), "round", {Float(16, 8)}, ArmIntrinsic::RequireFp16},
    {"llvm.roundeven", "llvm.roundeven", Float(32, 4), "round", {Float(32, 4)}},
    {"llvm.roundeven", "llvm.roundeven", Float(64, 2), "round", {Float(64, 2)}},
    {"llvm.roundeven.f16", "llvm.roundeven.f16", Float(16), "round", {Float(16)}, ArmIntrinsic::RequireFp16 | ArmIntrinsic::NoMangle},
    {"llvm.roundeven.f32", "llvm.roundeven.f32", Float(32), "round", {Float(32)}, ArmIntrinsic::NoMangle},
    {"llvm.roundeven.f64", "llvm.roundeven.f64", Float(64), "round", {Float(64)}, ArmIntrinsic::NoMangle},

    // SABD, UABD - Absolute difference
    {"vabds", "sabd", UInt(8, 8), "absd", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vabdu", "uabd", UInt(8, 8), "absd", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vabds", "sabd", UInt(16, 4), "absd", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vabdu", "uabd", UInt(16, 4), "absd", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vabds", "sabd", UInt(32, 2), "absd", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vabdu", "uabd", UInt(32, 2), "absd", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::HalfWidth},

    // SMULL, UMULL - Widening multiply
    {"vmulls", "smull", Int(16, 8), "widening_mul", {Int(8, 8), Int(8, 8)}},
    {"vmullu", "umull", UInt(16, 8), "widening_mul", {UInt(8, 8), UInt(8, 8)}},
    {"vmulls", "smull", Int(32, 4), "widening_mul", {Int(16, 4), Int(16, 4)}},
    {"vmullu", "umull", UInt(32, 4), "widening_mul", {UInt(16, 4), UInt(16, 4)}},
    {"vmulls", "smull", Int(64, 2), "widening_mul", {Int(32, 2), Int(32, 2)}},
    {"vmullu", "umull", UInt(64, 2), "widening_mul", {UInt(32, 2), UInt(32, 2)}},

    // SQADD, UQADD - Saturating add
    // On arm32, the ARM version of this seems to be missing on some configurations.
    // Rather than debug this, just use LLVM's saturating add intrinsic.
    {"llvm.sadd.sat", "sqadd", Int(8, 8), "saturating_add", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"llvm.uadd.sat", "uqadd", UInt(8, 8), "saturating_add", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::HalfWidth},
    {"llvm.sadd.sat", "sqadd", Int(16, 4), "saturating_add", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"llvm.uadd.sat", "uqadd", UInt(16, 4), "saturating_add", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::HalfWidth},
    {"llvm.sadd.sat", "sqadd", Int(32, 2), "saturating_add", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"llvm.uadd.sat", "uqadd", UInt(32, 2), "saturating_add", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::HalfWidth},

    // SQSUB, UQSUB - Saturating subtract
    {"llvm.ssub.sat", "sqsub", Int(8, 8), "saturating_sub", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"llvm.usub.sat", "uqsub", UInt(8, 8), "saturating_sub", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::HalfWidth},
    {"llvm.ssub.sat", "sqsub", Int(16, 4), "saturating_sub", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"llvm.usub.sat", "uqsub", UInt(16, 4), "saturating_sub", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::HalfWidth},
    {"llvm.ssub.sat", "sqsub", Int(32, 2), "saturating_sub", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"llvm.usub.sat", "uqsub", UInt(32, 2), "saturating_sub", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::HalfWidth},

    // SHADD, UHADD - Halving add
    {"vhadds", "shadd", Int(8, 8), "halving_add", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vhaddu", "uhadd", UInt(8, 8), "halving_add", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vhadds", "shadd", Int(16, 4), "halving_add", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vhaddu", "uhadd", UInt(16, 4), "halving_add", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vhadds", "shadd", Int(32, 2), "halving_add", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vhaddu", "uhadd", UInt(32, 2), "halving_add", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::HalfWidth},

    // SHSUB, UHSUB - Halving subtract
    {"vhsubs", "shsub", Int(8, 8), "halving_sub", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vhsubu", "uhsub", UInt(8, 8), "halving_sub", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vhsubs", "shsub", Int(16, 4), "halving_sub", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vhsubu", "uhsub", UInt(16, 4), "halving_sub", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vhsubs", "shsub", Int(32, 2), "halving_sub", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vhsubu", "uhsub", UInt(32, 2), "halving_sub", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::HalfWidth},

    // SRHADD, URHADD - Halving add with rounding
    {"vrhadds", "srhadd", Int(8, 8), "rounding_halving_add", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vrhaddu", "urhadd", UInt(8, 8), "rounding_halving_add", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vrhadds", "srhadd", Int(16, 4), "rounding_halving_add", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vrhaddu", "urhadd", UInt(16, 4), "rounding_halving_add", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vrhadds", "srhadd", Int(32, 2), "rounding_halving_add", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vrhaddu", "urhadd", UInt(32, 2), "rounding_halving_add", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::HalfWidth},

    // SMIN, UMIN, FMIN - Min
    {"vmins", "smin", Int(8, 8), "min", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vminu", "umin", UInt(8, 8), "min", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vmins", "smin", Int(16, 4), "min", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vminu", "umin", UInt(16, 4), "min", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vmins", "smin", Int(32, 2), "min", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vminu", "umin", UInt(32, 2), "min", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vmins", "fmin", Float(32, 2), "min", {Float(32, 2), Float(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vmins", "fmin", Float(16, 4), "min", {Float(16, 4), Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16},

    // FCVTZS, FCVTZU
    {nullptr, "fcvtzs", Int(16, 4), "fp_to_int", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::RequireFp16},
    {nullptr, "fcvtzu", UInt(16, 4), "fp_to_int", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::RequireFp16},

    // SMAX, UMAX, FMAX - Max
    {"vmaxs", "smax", Int(8, 8), "max", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vmaxu", "umax", UInt(8, 8), "max", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vmaxs", "smax", Int(16, 4), "max", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vmaxu", "umax", UInt(16, 4), "max", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vmaxs", "smax", Int(32, 2), "max", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vmaxu", "umax", UInt(32, 2), "max", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vmaxs", "fmax", Float(32, 2), "max", {Float(32, 2), Float(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vmaxs", "fmax", Float(16, 4), "max", {Float(16, 4), Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16},

    // SQNEG, UQNEG - Saturating negation
    {"vqneg", "sqneg", Int(8, 8), "saturating_negate", {Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vqneg", "sqneg", Int(16, 4), "saturating_negate", {Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vqneg", "sqneg", Int(32, 2), "saturating_negate", {Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vqneg", "sqneg", Int(64, 2), "saturating_negate", {Int(64, 2)}},

    // SQXTN, UQXTN, SQXTUN - Saturating narrowing
    {"vqmovns", "sqxtn", Int(8, 8), "saturating_narrow", {Int(16, 8)}},
    {"vqmovnu", "uqxtn", UInt(8, 8), "saturating_narrow", {UInt(16, 8)}},
    {"vqmovnsu", "sqxtun", UInt(8, 8), "saturating_narrow", {Int(16, 8)}},
    {"vqmovns", "sqxtn", Int(16, 4), "saturating_narrow", {Int(32, 4)}},
    {"vqmovnu", "uqxtn", UInt(16, 4), "saturating_narrow", {UInt(32, 4)}},
    {"vqmovnsu", "sqxtun", UInt(16, 4), "saturating_narrow", {Int(32, 4)}},
    {"vqmovns", "sqxtn", Int(32, 2), "saturating_narrow", {Int(64, 2)}},
    {"vqmovnu", "uqxtn", UInt(32, 2), "saturating_narrow", {UInt(64, 2)}},
    {"vqmovnsu", "sqxtun", UInt(32, 2), "saturating_narrow", {Int(64, 2)}},

    // RSHRN - Rounding shift right narrow (by immediate in [1, output bits])
    // arm32 expects a vector RHS of the same type as the LHS except signed.
    {"vrshiftn", nullptr, Int(8, 8), "rounding_shift_right_narrow", {Int(16, 8), Int(16, 8)}},
    {"vrshiftn", nullptr, UInt(8, 8), "rounding_shift_right_narrow", {UInt(16, 8), Int(16, 8)}},
    {"vrshiftn", nullptr, Int(16, 4), "rounding_shift_right_narrow", {Int(32, 4), Int(32, 4)}},
    {"vrshiftn", nullptr, UInt(16, 4), "rounding_shift_right_narrow", {UInt(32, 4), Int(32, 4)}},
    {"vrshiftn", nullptr, Int(32, 2), "rounding_shift_right_narrow", {Int(64, 2), Int(64, 2)}},
    {"vrshiftn", nullptr, UInt(32, 2), "rounding_shift_right_narrow", {UInt(64, 2), Int(64, 2)}},

    // arm64 expects a 32-bit constant.
    {nullptr, "rshrn", Int(8, 8), "rounding_shift_right_narrow", {Int(16, 8), UInt(32)}},
    {nullptr, "rshrn", UInt(8, 8), "rounding_shift_right_narrow", {UInt(16, 8), UInt(32)}},
    {nullptr, "rshrn", Int(16, 4), "rounding_shift_right_narrow", {Int(32, 4), UInt(32)}},
    {nullptr, "rshrn", UInt(16, 4), "rounding_shift_right_narrow", {UInt(32, 4), UInt(32)}},
    {nullptr, "rshrn", Int(32, 2), "rounding_shift_right_narrow", {Int(64, 2), UInt(32)}},
    {nullptr, "rshrn", UInt(32, 2), "rounding_shift_right_narrow", {UInt(64, 2), UInt(32)}},

    // SHRN - Shift right narrow (by immediate in [1, output bits])
    // LLVM pattern matches these.

    // SQRSHL, UQRSHL - Saturating rounding shift left (by signed vector)
    {"vqrshifts", "sqrshl", Int(8, 8), "saturating_rounding_shift_left", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vqrshiftu", "uqrshl", UInt(8, 8), "saturating_rounding_shift_left", {UInt(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vqrshifts", "sqrshl", Int(16, 4), "saturating_rounding_shift_left", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vqrshiftu", "uqrshl", UInt(16, 4), "saturating_rounding_shift_left", {UInt(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vqrshifts", "sqrshl", Int(32, 2), "saturating_rounding_shift_left", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vqrshiftu", "uqrshl", UInt(32, 2), "saturating_rounding_shift_left", {UInt(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vqrshifts", "sqrshl", Int(64, 2), "saturating_rounding_shift_left", {Int(64, 2), Int(64, 2)}},
    {"vqrshiftu", "uqrshl", UInt(64, 2), "saturating_rounding_shift_left", {UInt(64, 2), Int(64, 2)}},

    // SQRSHRN, UQRSHRN, SQRSHRUN - Saturating rounding narrowing shift right (by immediate in [1, output bits])
    // arm32 expects a vector RHS of the same type as the LHS except signed.
    {"vqrshiftns", nullptr, Int(8, 8), "saturating_rounding_shift_right_narrow", {Int(16, 8), Int(16, 8)}},
    {"vqrshiftnu", nullptr, UInt(8, 8), "saturating_rounding_shift_right_narrow", {UInt(16, 8), Int(16, 8)}},
    {"vqrshiftnsu", nullptr, UInt(8, 8), "saturating_rounding_shift_right_narrow", {Int(16, 8), Int(16, 8)}},
    {"vqrshiftns", nullptr, Int(16, 4), "saturating_rounding_shift_right_narrow", {Int(32, 4), Int(32, 4)}},
    {"vqrshiftnu", nullptr, UInt(16, 4), "saturating_rounding_shift_right_narrow", {UInt(32, 4), Int(32, 4)}},
    {"vqrshiftnsu", nullptr, UInt(16, 4), "saturating_rounding_shift_right_narrow", {Int(32, 4), Int(32, 4)}},
    {"vqrshiftns", nullptr, Int(32, 2), "saturating_rounding_shift_right_narrow", {Int(64, 2), Int(64, 2)}},
    {"vqrshiftnu", nullptr, UInt(32, 2), "saturating_rounding_shift_right_narrow", {UInt(64, 2), Int(64, 2)}},
    {"vqrshiftnsu", nullptr, UInt(32, 2), "saturating_rounding_shift_right_narrow", {Int(64, 2), Int(64, 2)}},

    // arm64 expects a 32-bit constant.
    {nullptr, "sqrshrn", Int(8, 8), "saturating_rounding_shift_right_narrow", {Int(16, 8), UInt(32)}},
    {nullptr, "uqrshrn", UInt(8, 8), "saturating_rounding_shift_right_narrow", {UInt(16, 8), UInt(32)}},
    {nullptr, "sqrshrun", UInt(8, 8), "saturating_rounding_shift_right_narrow", {Int(16, 8), UInt(32)}},
    {nullptr, "sqrshrn", Int(16, 4), "saturating_rounding_shift_right_narrow", {Int(32, 4), UInt(32)}},
    {nullptr, "uqrshrn", UInt(16, 4), "saturating_rounding_shift_right_narrow", {UInt(32, 4), UInt(32)}},
    {nullptr, "sqrshrun", UInt(16, 4), "saturating_rounding_shift_right_narrow", {Int(32, 4), UInt(32)}},
    {nullptr, "sqrshrn", Int(32, 2), "saturating_rounding_shift_right_narrow", {Int(64, 2), UInt(32)}},
    {nullptr, "uqrshrn", UInt(32, 2), "saturating_rounding_shift_right_narrow", {UInt(64, 2), UInt(32)}},
    {nullptr, "sqrshrun", UInt(32, 2), "saturating_rounding_shift_right_narrow", {Int(64, 2), UInt(32)}},

    // SQSHL, UQSHL, SQSHLU - Saturating shift left by signed register.
    // There is also an immediate version of this - hopefully LLVM does this matching when appropriate.
    {"vqshifts", "sqshl", Int(8, 8), "saturating_shift_left", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshiftu", "uqshl", UInt(8, 8), "saturating_shift_left", {UInt(8, 8), Int(8, 8)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshiftsu", "sqshlu", UInt(8, 8), "saturating_shift_left", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshifts", "sqshl", Int(16, 4), "saturating_shift_left", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshiftu", "uqshl", UInt(16, 4), "saturating_shift_left", {UInt(16, 4), Int(16, 4)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshiftsu", "sqshlu", UInt(16, 4), "saturating_shift_left", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshifts", "sqshl", Int(32, 2), "saturating_shift_left", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshiftu", "uqshl", UInt(32, 2), "saturating_shift_left", {UInt(32, 2), Int(32, 2)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshiftsu", "sqshlu", UInt(32, 2), "saturating_shift_left", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshifts", "sqshl", Int(64, 2), "saturating_shift_left", {Int(64, 2), Int(64, 2)}, ArmIntrinsic::AllowUnsignedOp1},
    {"vqshiftu", "uqshl", UInt(64, 2), "saturating_shift_left", {UInt(64, 2), Int(64, 2)}, ArmIntrinsic::AllowUnsignedOp1},
    {"vqshiftsu", "sqshlu", UInt(64, 2), "saturating_shift_left", {Int(64, 2), Int(64, 2)}, ArmIntrinsic::AllowUnsignedOp1},

    // SQSHRN, UQSHRN, SQRSHRUN Saturating narrowing shift right by an (by immediate in [1, output bits])
    // arm32 expects a vector RHS of the same type as the LHS.
    {"vqshiftns", nullptr, Int(8, 8), "saturating_shift_right_narrow", {Int(16, 8), Int(16, 8)}},
    {"vqshiftnu", nullptr, UInt(8, 8), "saturating_shift_right_narrow", {UInt(16, 8), Int(16, 8)}},
    {"vqshiftns", nullptr, Int(16, 4), "saturating_shift_right_narrow", {Int(32, 4), Int(32, 4)}},
    {"vqshiftnu", nullptr, UInt(16, 4), "saturating_shift_right_narrow", {UInt(32, 4), Int(32, 4)}},
    {"vqshiftns", nullptr, Int(32, 2), "saturating_shift_right_narrow", {Int(64, 2), Int(64, 2)}},
    {"vqshiftnu", nullptr, UInt(32, 2), "saturating_shift_right_narrow", {UInt(64, 2), Int(64, 2)}},
    {"vqshiftnsu", nullptr, UInt(8, 8), "saturating_shift_right_narrow", {Int(16, 8), Int(16, 8)}},
    {"vqshiftnsu", nullptr, UInt(16, 4), "saturating_shift_right_narrow", {Int(32, 4), Int(32, 4)}},
    {"vqshiftnsu", nullptr, UInt(32, 2), "saturating_shift_right_narrow", {Int(64, 2), Int(64, 2)}},

    // arm64 expects a 32-bit constant.
    {nullptr, "sqshrn", Int(8, 8), "saturating_shift_right_narrow", {Int(16, 8), UInt(32)}},
    {nullptr, "uqshrn", UInt(8, 8), "saturating_shift_right_narrow", {UInt(16, 8), UInt(32)}},
    {nullptr, "sqshrn", Int(16, 4), "saturating_shift_right_narrow", {Int(32, 4), UInt(32)}},
    {nullptr, "uqshrn", UInt(16, 4), "saturating_shift_right_narrow", {UInt(32, 4), UInt(32)}},
    {nullptr, "sqshrn", Int(32, 2), "saturating_shift_right_narrow", {Int(64, 2), UInt(32)}},
    {nullptr, "uqshrn", UInt(32, 2), "saturating_shift_right_narrow", {UInt(64, 2), UInt(32)}},
    {nullptr, "sqshrun", UInt(8, 8), "saturating_shift_right_narrow", {Int(16, 8), UInt(32)}},
    {nullptr, "sqshrun", UInt(16, 4), "saturating_shift_right_narrow", {Int(32, 4), UInt(32)}},
    {nullptr, "sqshrun", UInt(32, 2), "saturating_shift_right_narrow", {Int(64, 2), UInt(32)}},

    // SRSHL, URSHL - Rounding shift left (by signed vector)
    {"vrshifts", "srshl", Int(8, 8), "rounding_shift_left", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vrshiftu", "urshl", UInt(8, 8), "rounding_shift_left", {UInt(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vrshifts", "srshl", Int(16, 4), "rounding_shift_left", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vrshiftu", "urshl", UInt(16, 4), "rounding_shift_left", {UInt(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vrshifts", "srshl", Int(32, 2), "rounding_shift_left", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vrshiftu", "urshl", UInt(32, 2), "rounding_shift_left", {UInt(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vrshifts", "srshl", Int(64, 2), "rounding_shift_left", {Int(64, 2), Int(64, 2)}},
    {"vrshiftu", "urshl", UInt(64, 2), "rounding_shift_left", {UInt(64, 2), Int(64, 2)}},

    // SSHL, USHL - Shift left (by signed vector)
    {"vshifts", "sshl", Int(8, 8), "shift_left", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vshiftu", "ushl", UInt(8, 8), "shift_left", {UInt(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vshifts", "sshl", Int(16, 4), "shift_left", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vshiftu", "ushl", UInt(16, 4), "shift_left", {UInt(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vshifts", "sshl", Int(32, 2), "shift_left", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vshiftu", "ushl", UInt(32, 2), "shift_left", {UInt(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vshifts", "sshl", Int(64, 2), "shift_left", {Int(64, 2), Int(64, 2)}},
    {"vshiftu", "ushl", UInt(64, 2), "shift_left", {UInt(64, 2), Int(64, 2)}},

    // SRSHR, URSHR - Rounding shift right (by immediate in [1, output bits])
    // LLVM wants these expressed as SRSHL by negative amounts.

    // SSHLL, USHLL - Shift left long (by immediate in [0, output bits - 1])
    // LLVM pattern matches these for us.

    // RADDHN - Add and narrow with rounding.
    {"vraddhn", "raddhn", Int(8, 8), "rounding_add_narrow", {Int(16, 8), Int(16, 8)}},
    {"vraddhn", "raddhn", UInt(8, 8), "rounding_add_narrow", {UInt(16, 8), UInt(16, 8)}},
    {"vraddhn", "raddhn", Int(16, 4), "rounding_add_narrow", {Int(32, 4), Int(32, 4)}},
    {"vraddhn", "raddhn", UInt(16, 4), "rounding_add_narrow", {UInt(32, 4), UInt(32, 4)}},
    {"vraddhn", "raddhn", Int(32, 2), "rounding_add_narrow", {Int(64, 2), Int(64, 2)}},
    {"vraddhn", "raddhn", UInt(32, 2), "rounding_add_narrow", {UInt(64, 2), UInt(64, 2)}},

    // RSUBHN - Sub and narrow with rounding.
    {"vrsubhn", "rsubhn", Int(8, 8), "rounding_sub_narrow", {Int(16, 8), Int(16, 8)}},
    {"vrsubhn", "rsubhn", UInt(8, 8), "rounding_sub_narrow", {UInt(16, 8), UInt(16, 8)}},
    {"vrsubhn", "rsubhn", Int(16, 4), "rounding_sub_narrow", {Int(32, 4), Int(32, 4)}},
    {"vrsubhn", "rsubhn", UInt(16, 4), "rounding_sub_narrow", {UInt(32, 4), UInt(32, 4)}},
    {"vrsubhn", "rsubhn", Int(32, 2), "rounding_sub_narrow", {Int(64, 2), Int(64, 2)}},
    {"vrsubhn", "rsubhn", UInt(32, 2), "rounding_sub_narrow", {UInt(64, 2), UInt(64, 2)}},

    // SQDMULH - Saturating doubling multiply keep high half.
    {"vqdmulh", "sqdmulh", Int(16, 4), "qdmulh", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vqdmulh", "sqdmulh", Int(32, 2), "qdmulh", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},

    // SQRDMULH - Saturating doubling multiply keep high half with rounding.
    {"vqrdmulh", "sqrdmulh", Int(16, 4), "qrdmulh", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vqrdmulh", "sqrdmulh", Int(32, 2), "qrdmulh", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},

    // PADD - Pairwise add.
    // 32-bit only has half-width versions.
    {"vpadd", nullptr, Int(8, 8), "pairwise_add", {Int(8, 16)}, ArmIntrinsic::SplitArg0},
    {"vpadd", nullptr, UInt(8, 8), "pairwise_add", {UInt(8, 16)}, ArmIntrinsic::SplitArg0},
    {"vpadd", nullptr, Int(16, 4), "pairwise_add", {Int(16, 8)}, ArmIntrinsic::SplitArg0},
    {"vpadd", nullptr, UInt(16, 4), "pairwise_add", {UInt(16, 8)}, ArmIntrinsic::SplitArg0},
    {"vpadd", nullptr, Int(32, 2), "pairwise_add", {Int(32, 4)}, ArmIntrinsic::SplitArg0},
    {"vpadd", nullptr, UInt(32, 2), "pairwise_add", {UInt(32, 4)}, ArmIntrinsic::SplitArg0},
    {"vpadd", nullptr, Float(32, 2), "pairwise_add", {Float(32, 4)}, ArmIntrinsic::SplitArg0},
    {"vpadd", nullptr, Float(16, 4), "pairwise_add", {Float(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::RequireFp16},

    {nullptr, "addp", Int(8, 8), "pairwise_add", {Int(8, 16)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "addp", UInt(8, 8), "pairwise_add", {UInt(8, 16)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "addp", Int(16, 4), "pairwise_add", {Int(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "addp", UInt(16, 4), "pairwise_add", {UInt(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "addp", Int(32, 2), "pairwise_add", {Int(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "addp", UInt(32, 2), "pairwise_add", {UInt(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "faddp", Float(32, 2), "pairwise_add", {Float(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "faddp", Float(64, 2), "pairwise_add", {Float(64, 4)}, ArmIntrinsic::SplitArg0},
    {nullptr, "faddp", Float(16, 4), "pairwise_add", {Float(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16},

    // SADDLP, UADDLP - Pairwise add long.
    {"vpaddls", "saddlp", Int(16, 4), "pairwise_widening_add", {Int(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs},
    {"vpaddlu", "uaddlp", UInt(16, 4), "pairwise_widening_add", {UInt(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs},
    {"vpaddlu", "uaddlp", Int(16, 4), "pairwise_widening_add", {UInt(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs},
    {"vpaddls", "saddlp", Int(32, 2), "pairwise_widening_add", {Int(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs},
    {"vpaddlu", "uaddlp", UInt(32, 2), "pairwise_widening_add", {UInt(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs},
    {"vpaddlu", "uaddlp", Int(32, 2), "pairwise_widening_add", {UInt(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs},
    {"vpaddls", "saddlp", Int(64, 1), "pairwise_widening_add", {Int(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::ScalarsAreVectors},
    {"vpaddlu", "uaddlp", UInt(64, 1), "pairwise_widening_add", {UInt(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::ScalarsAreVectors},
    {"vpaddlu", "uaddlp", Int(64, 1), "pairwise_widening_add", {UInt(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::ScalarsAreVectors},

    // SPADAL, UPADAL - Pairwise add and accumulate long.
    {"vpadals", nullptr, Int(16, 4), "pairwise_widening_add_accumulate", {Int(16, 4), Int(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs},
    {"vpadalu", nullptr, UInt(16, 4), "pairwise_widening_add_accumulate", {UInt(16, 4), UInt(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs},
    {"vpadalu", nullptr, Int(16, 4), "pairwise_widening_add_accumulate", {Int(16, 4), UInt(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs},
    {"vpadals", nullptr, Int(32, 2), "pairwise_widening_add_accumulate", {Int(32, 2), Int(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs},
    {"vpadalu", nullptr, UInt(32, 2), "pairwise_widening_add_accumulate", {UInt(32, 2), UInt(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs},
    {"vpadalu", nullptr, Int(32, 2), "pairwise_widening_add_accumulate", {Int(32, 2), UInt(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs},
    {"vpadals", nullptr, Int(64, 1), "pairwise_widening_add_accumulate", {Int(64, 1), Int(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs | ArmIntrinsic::ScalarsAreVectors},
    {"vpadalu", nullptr, UInt(64, 1), "pairwise_widening_add_accumulate", {UInt(64, 1), UInt(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs | ArmIntrinsic::ScalarsAreVectors},
    {"vpadalu", nullptr, Int(64, 1), "pairwise_widening_add_accumulate", {Int(64, 1), UInt(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs | ArmIntrinsic::ScalarsAreVectors},

    // SMAXP, UMAXP, FMAXP - Pairwise max.
    {nullptr, "smaxp", Int(8, 8), "pairwise_max", {Int(8, 16)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "umaxp", UInt(8, 8), "pairwise_max", {UInt(8, 16)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "smaxp", Int(16, 4), "pairwise_max", {Int(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "umaxp", UInt(16, 4), "pairwise_max", {UInt(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "smaxp", Int(32, 2), "pairwise_max", {Int(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "umaxp", UInt(32, 2), "pairwise_max", {UInt(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "fmaxp", Float(32, 2), "pairwise_max", {Float(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "fmaxp", Float(16, 4), "pairwise_max", {Float(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16},

    // On arm32, we only have half-width versions of these.
    {"vpmaxs", nullptr, Int(8, 8), "pairwise_max", {Int(8, 16)}, ArmIntrinsic::SplitArg0},
    {"vpmaxu", nullptr, UInt(8, 8), "pairwise_max", {UInt(8, 16)}, ArmIntrinsic::SplitArg0},
    {"vpmaxs", nullptr, Int(16, 4), "pairwise_max", {Int(16, 8)}, ArmIntrinsic::SplitArg0},
    {"vpmaxu", nullptr, UInt(16, 4), "pairwise_max", {UInt(16, 8)}, ArmIntrinsic::SplitArg0},
    {"vpmaxs", nullptr, Int(32, 2), "pairwise_max", {Int(32, 4)}, ArmIntrinsic::SplitArg0},
    {"vpmaxu", nullptr, UInt(32, 2), "pairwise_max", {UInt(32, 4)}, ArmIntrinsic::SplitArg0},
    {"vpmaxs", nullptr, Float(32, 2), "pairwise_max", {Float(32, 4)}, ArmIntrinsic::SplitArg0},
    {"vpmaxs", nullptr, Float(16, 4), "pairwise_max", {Float(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::RequireFp16},

    // SMINP, UMINP, FMINP - Pairwise min.
    {nullptr, "sminp", Int(8, 8), "pairwise_min", {Int(8, 16)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "uminp", UInt(8, 8), "pairwise_min", {UInt(8, 16)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "sminp", Int(16, 4), "pairwise_min", {Int(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "uminp", UInt(16, 4), "pairwise_min", {UInt(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "sminp", Int(32, 2), "pairwise_min", {Int(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "uminp", UInt(32, 2), "pairwise_min", {UInt(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "fminp", Float(32, 2), "pairwise_min", {Float(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth},
    {nullptr, "fminp", Float(16, 4), "pairwise_min", {Float(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16},

    // On arm32, we only have half-width versions of these.
    {"vpmins", nullptr, Int(8, 8), "pairwise_min", {Int(8, 16)}, ArmIntrinsic::SplitArg0},
    {"vpminu", nullptr, UInt(8, 8), "pairwise_min", {UInt(8, 16)}, ArmIntrinsic::SplitArg0},
    {"vpmins", nullptr, Int(16, 4), "pairwise_min", {Int(16, 8)}, ArmIntrinsic::SplitArg0},
    {"vpminu", nullptr, UInt(16, 4), "pairwise_min", {UInt(16, 8)}, ArmIntrinsic::SplitArg0},
    {"vpmins", nullptr, Int(32, 2), "pairwise_min", {Int(32, 4)}, ArmIntrinsic::SplitArg0},
    {"vpminu", nullptr, UInt(32, 2), "pairwise_min", {UInt(32, 4)}, ArmIntrinsic::SplitArg0},
    {"vpmins", nullptr, Float(32, 2), "pairwise_min", {Float(32, 4)}, ArmIntrinsic::SplitArg0},
    {"vpmins", nullptr, Float(16, 4), "pairwise_min", {Float(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::RequireFp16},

    // SDOT, UDOT - Dot products.
    // Mangle this one manually, there aren't that many and it is a special case.
    {nullptr, "sdot.v2i32.v8i8", Int(32, 2), "dot_product", {Int(32, 2), Int(8, 8), Int(8, 8)}, ArmIntrinsic::NoMangle},
    {nullptr, "udot.v2i32.v8i8", Int(32, 2), "dot_product", {Int(32, 2), UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::NoMangle},
    {nullptr, "udot.v2i32.v8i8", UInt(32, 2), "dot_product", {UInt(32, 2), UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::NoMangle},
    {nullptr, "sdot.v4i32.v16i8", Int(32, 4), "dot_product", {Int(32, 4), Int(8, 16), Int(8, 16)}, ArmIntrinsic::NoMangle},
    {nullptr, "udot.v4i32.v16i8", Int(32, 4), "dot_product", {Int(32, 4), UInt(8, 16), UInt(8, 16)}, ArmIntrinsic::NoMangle},
    {nullptr, "udot.v4i32.v16i8", UInt(32, 4), "dot_product", {UInt(32, 4), UInt(8, 16), UInt(8, 16)}, ArmIntrinsic::NoMangle},

    // ABDL - Widening absolute difference
    // The ARM backend folds both signed and unsigned widening casts of absd to a widening_absd, so we need to handle both signed and
    // unsigned input and return types.
    {"vabdl_i8x8", "vabdl_i8x8", Int(16, 8), "widening_absd", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix},
    {"vabdl_i8x8", "vabdl_i8x8", UInt(16, 8), "widening_absd", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix},
    {"vabdl_u8x8", "vabdl_u8x8", Int(16, 8), "widening_absd", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix},
    {"vabdl_u8x8", "vabdl_u8x8", UInt(16, 8), "widening_absd", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix},
    {"vabdl_i16x4", "vabdl_i16x4", Int(32, 4), "widening_absd", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix},
    {"vabdl_i16x4", "vabdl_i16x4", UInt(32, 4), "widening_absd", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix},
    {"vabdl_u16x4", "vabdl_u16x4", Int(32, 4), "widening_absd", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix},
    {"vabdl_u16x4", "vabdl_u16x4", UInt(32, 4), "widening_absd", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix},
    {"vabdl_i32x2", "vabdl_i32x2", Int(64, 2), "widening_absd", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix},
    {"vabdl_i32x2", "vabdl_i32x2", UInt(64, 2), "widening_absd", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix},
    {"vabdl_u32x2", "vabdl_u32x2", Int(64, 2), "widening_absd", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix},
    {"vabdl_u32x2", "vabdl_u32x2", UInt(64, 2), "widening_absd", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix},
};

// List of fp16 math functions which we can avoid "emulated" equivalent code generation.
// Only possible if the target has ARMFp16 feature.

// These can be vectorized as fp16 SIMD instruction
const std::set<string> float16_native_funcs = {
    "ceil_f16",
    "floor_f16",
    "is_finite_f16",
    "is_inf_f16",
    "is_nan_f16",
    "sqrt_f16",
    "trunc_f16",
};

// These end up with fp32 math function call.
// However, data type conversion of fp16 <-> fp32 is performed natively rather than emulation.
// SIMD instruction is not available, so scalar based instruction is generated.
const std::map<string, string> float16_transcendental_remapping = {
    {"acos_f16", "acos_f32"},
    {"acosh_f16", "acosh_f32"},
    {"asin_f16", "asin_f32"},
    {"asinh_f16", "asinh_f32"},
    {"atan_f16", "atan_f32"},
    {"atan2_f16", "atan2_f32"},
    {"atanh_f16", "atanh_f32"},
    {"cos_f16", "cos_f32"},
    {"cosh_f16", "cosh_f32"},
    {"exp_f16", "exp_f32"},
    {"log_f16", "log_f32"},
    {"pow_f16", "pow_f32"},
    {"sin_f16", "sin_f32"},
    {"sinh_f16", "sinh_f32"},
    {"tan_f16", "tan_f32"},
    {"tanh_f16", "tanh_f32"},
};
// clang-format on

llvm::Function *CodeGen_ARM::define_concat_args_wrapper(llvm::Function *inner, const string &name) {
    llvm::FunctionType *inner_ty = inner->getFunctionType();

    internal_assert(inner_ty->getNumParams() == 2);
    llvm::Type *inner_arg0_ty = inner_ty->getParamType(0);
    llvm::Type *inner_arg1_ty = inner_ty->getParamType(1);
    int inner_arg0_lanes = get_vector_num_elements(inner_arg0_ty);
    int inner_arg1_lanes = get_vector_num_elements(inner_arg1_ty);

    llvm::Type *concat_arg_ty =
        get_vector_type(inner_arg0_ty->getScalarType(), inner_arg0_lanes + inner_arg1_lanes);

    // Make a wrapper.
    llvm::FunctionType *wrapper_ty =
        llvm::FunctionType::get(inner_ty->getReturnType(), {concat_arg_ty}, false);
    llvm::Function *wrapper =
        llvm::Function::Create(wrapper_ty, llvm::GlobalValue::InternalLinkage, name, module.get());
    llvm::BasicBlock *block =
        llvm::BasicBlock::Create(module->getContext(), "entry", wrapper);
    IRBuilderBase::InsertPoint here = builder->saveIP();
    builder->SetInsertPoint(block);

    // Call the real intrinsic.
    Value *low = slice_vector(wrapper->getArg(0), 0, inner_arg0_lanes);
    Value *high = slice_vector(wrapper->getArg(0), inner_arg0_lanes, inner_arg1_lanes);
    Value *ret = builder->CreateCall(inner, {low, high});
    builder->CreateRet(ret);

    // Always inline these wrappers.
    wrapper->addFnAttr(llvm::Attribute::AlwaysInline);

    builder->restoreIP(here);

    llvm::verifyFunction(*wrapper);
    return wrapper;
}

void CodeGen_ARM::init_module() {
    CodeGen_Posix::init_module();

    if (neon_intrinsics_disabled()) {
        return;
    }

    string prefix = target.bits == 32 ? "llvm.arm.neon." : "llvm.aarch64.neon.";
    for (const ArmIntrinsic &intrin : intrinsic_defs) {
        if (intrin.flags & ArmIntrinsic::RequireFp16 && !target.has_feature(Target::ARMFp16)) {
            continue;
        }
        // Get the name of the intrinsic with the appropriate prefix.
        const char *intrin_name = nullptr;
        if (target.bits == 32) {
            intrin_name = intrin.arm32;
        } else {
            intrin_name = intrin.arm64;
        }
        if (!intrin_name) {
            continue;
        }
        string full_name = intrin_name;
        if (!starts_with(full_name, "llvm.") && (intrin.flags & ArmIntrinsic::NoPrefix) == 0) {
            full_name = prefix + full_name;
        }

        // We might have to generate versions of this intrinsic with multiple widths.
        vector<int> width_factors = {1};
        if (intrin.flags & ArmIntrinsic::HalfWidth) {
            width_factors.push_back(2);
        }

        for (int width_factor : width_factors) {
            Type ret_type = intrin.ret_type;
            ret_type = ret_type.with_lanes(ret_type.lanes() * width_factor);
            internal_assert(ret_type.bits() * ret_type.lanes() <= 128) << full_name << "\n";
            vector<Type> arg_types;
            arg_types.reserve(4);
            for (halide_type_t i : intrin.arg_types) {
                if (i.bits == 0) {
                    break;
                }
                Type arg_type = i;
                if (arg_type.is_vector()) {
                    arg_type = arg_type.with_lanes(arg_type.lanes() * width_factor);
                }
                arg_types.emplace_back(arg_type);
            }

            // Generate the LLVM mangled name.
            std::stringstream mangled_name_builder;
            mangled_name_builder << full_name;
            if (starts_with(full_name, "llvm.") && (intrin.flags & ArmIntrinsic::NoMangle) == 0) {
                // Append LLVM name mangling for either the return type or the arguments, or both.
                vector<Type> types;
                if (intrin.flags & ArmIntrinsic::MangleArgs) {
                    types = arg_types;
                } else if (intrin.flags & ArmIntrinsic::MangleRetArgs) {
                    types = {ret_type};
                    types.insert(types.end(), arg_types.begin(), arg_types.end());
                } else {
                    types = {ret_type};
                }
                for (const Type &t : types) {
                    mangled_name_builder << ".v" << t.lanes();
                    if (t.is_int() || t.is_uint()) {
                        mangled_name_builder << "i";
                    } else if (t.is_float()) {
                        mangled_name_builder << "f";
                    }
                    mangled_name_builder << t.bits();
                }
            }
            string mangled_name = mangled_name_builder.str();

            llvm::Function *intrin_impl = nullptr;
            if (intrin.flags & ArmIntrinsic::SplitArg0) {
                // This intrinsic needs a wrapper to split the argument.
                string wrapper_name = intrin.name + unique_name("_wrapper");
                Type split_arg_type = arg_types[0].with_lanes(arg_types[0].lanes() / 2);
                llvm::Function *to_wrap = get_llvm_intrin(ret_type, mangled_name, {split_arg_type, split_arg_type});
                intrin_impl = define_concat_args_wrapper(to_wrap, wrapper_name);
            } else {
                bool scalars_are_vectors = intrin.flags & ArmIntrinsic::ScalarsAreVectors;
                intrin_impl = get_llvm_intrin(ret_type, mangled_name, arg_types, scalars_are_vectors);
            }

            function_does_not_access_memory(intrin_impl);
            intrin_impl->addFnAttr(llvm::Attribute::NoUnwind);
            declare_intrin_overload(intrin.name, ret_type, intrin_impl, arg_types);
            if (intrin.flags & ArmIntrinsic::AllowUnsignedOp1) {
                // Also generate a version of this intrinsic where the second operand is unsigned.
                arg_types[1] = arg_types[1].with_code(halide_type_uint);
                declare_intrin_overload(intrin.name, ret_type, intrin_impl, arg_types);
            }
        }
    }
}

void CodeGen_ARM::visit(const Cast *op) {
    if (!neon_intrinsics_disabled() && op->type.is_vector()) {
        vector<Expr> matches;
        for (const Pattern &pattern : casts) {
            if (expr_match(pattern.pattern, op, matches)) {
                if (pattern.intrin.find("shift_right_narrow") != string::npos) {
                    // The shift_right_narrow patterns need the shift to be constant in [1, output_bits].
                    const uint64_t *const_b = as_const_uint(matches[1]);
                    if (!const_b || *const_b == 0 || (int)*const_b > op->type.bits()) {
                        continue;
                    }
                }
                if (target.bits == 32 && pattern.intrin.find("shift_right") != string::npos) {
                    // The 32-bit ARM backend wants right shifts as negative values.
                    matches[1] = simplify(-cast(matches[1].type().with_code(halide_type_int), matches[1]));
                }
                value = call_overloaded_intrin(op->type, pattern.intrin, matches);
                if (value) {
                    return;
                }
            }
        }

        // Catch signed widening of absolute difference.
        // Catch widening of absolute difference
        Type t = op->type;
        if ((t.is_int() || t.is_uint()) &&
            (op->value.type().is_int() || op->value.type().is_uint()) &&
            t.bits() == op->value.type().bits() * 2) {
            if (const Call *absd = Call::as_intrinsic(op->value, {Call::absd})) {
                value = call_overloaded_intrin(t, "widening_absd", absd->args);
                return;
            }
        }
    }

    // LLVM fptoui generates fcvtzs if src is fp16 scalar else fcvtzu.
    // To avoid that, we use neon intrinsic explicitly.
    if (is_float16_and_has_feature(op->value.type())) {
        if (op->type.is_int_or_uint() && op->type.bits() == 16) {
            value = call_overloaded_intrin(op->type, "fp_to_int", {op->value});
            if (value) {
                return;
            }
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Sub *op) {
    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    if (op->type.is_vector()) {
        vector<Expr> matches;
        for (const auto &i : negations) {
            if (expr_match(i.pattern, op, matches)) {
                value = call_overloaded_intrin(op->type, i.intrin, matches);
                return;
            }
        }
    }

    // llvm will generate floating point negate instructions if we ask for (-0.0f)-x
    if (op->type.is_float() &&
        (op->type.bits() >= 32 || is_float16_and_has_feature(op->type)) &&
        is_const_zero(op->a)) {
        Constant *a;
        if (op->type.bits() == 16) {
            a = ConstantFP::getNegativeZero(f16_t);
        } else if (op->type.bits() == 32) {
            a = ConstantFP::getNegativeZero(f32_t);
        } else if (op->type.bits() == 64) {
            a = ConstantFP::getNegativeZero(f64_t);
        } else {
            a = nullptr;
            internal_error << "Unknown bit width for floating point type: " << op->type << "\n";
        }

        Value *b = codegen(op->b);

        if (op->type.lanes() > 1) {
            a = get_splat(op->type.lanes(), a);
        }
        value = builder->CreateFSub(a, b);
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Min *op) {
    // Use a 2-wide vector for scalar floats.
    if (!neon_intrinsics_disabled() && (op->type == Float(32) || op->type.is_vector())) {
        value = call_overloaded_intrin(op->type, "min", {op->a, op->b});
        if (value) {
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Max *op) {
    // Use a 2-wide vector for scalar floats.
    if (!neon_intrinsics_disabled() && (op->type == Float(32) || op->type.is_vector())) {
        value = call_overloaded_intrin(op->type, "max", {op->a, op->b});
        if (value) {
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Store *op) {
    // Predicated store
    if (!is_const_one(op->predicate)) {
        CodeGen_Posix::visit(op);
        return;
    }

    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    // A dense store of an interleaving can be done using a vst2 intrinsic
    const Ramp *ramp = op->index.as<Ramp>();

    // We only deal with ramps here
    if (!ramp) {
        CodeGen_Posix::visit(op);
        return;
    }

    // First dig through let expressions
    Expr rhs = op->value;
    vector<pair<string, Expr>> lets;
    while (const Let *let = rhs.as<Let>()) {
        rhs = let->body;
        lets.emplace_back(let->name, let->value);
    }
    const Shuffle *shuffle = rhs.as<Shuffle>();

    // Interleaving store instructions only exist for certain types.
    bool type_ok_for_vst = false;
    Type intrin_type = Handle();
    if (shuffle) {
        Type t = shuffle->vectors[0].type();
        intrin_type = t;
        Type elt = t.element_of();
        int vec_bits = t.bits() * t.lanes();
        if (elt == Float(32) ||
            is_float16_and_has_feature(elt) ||
            elt == Int(8) || elt == Int(16) || elt == Int(32) ||
            elt == UInt(8) || elt == UInt(16) || elt == UInt(32)) {
            if (vec_bits % 128 == 0) {
                type_ok_for_vst = true;
                intrin_type = intrin_type.with_lanes(128 / t.bits());
            } else if (vec_bits % 64 == 0) {
                type_ok_for_vst = true;
                intrin_type = intrin_type.with_lanes(64 / t.bits());
            }
        }
    }

    if (is_const_one(ramp->stride) &&
        shuffle && shuffle->is_interleave() &&
        type_ok_for_vst &&
        2 <= shuffle->vectors.size() && shuffle->vectors.size() <= 4) {

        const int num_vecs = shuffle->vectors.size();
        vector<Value *> args(num_vecs);

        Type t = shuffle->vectors[0].type();

        // Assume element-aligned.
        int alignment = t.bytes();

        // Codegen the lets
        for (auto &let : lets) {
            sym_push(let.first, codegen(let.second));
        }

        // Codegen all the vector args.
        for (int i = 0; i < num_vecs; ++i) {
            args[i] = codegen(shuffle->vectors[i]);
        }

        // Declare the function
        std::ostringstream instr;
        vector<llvm::Type *> arg_types;
        llvm::Type *intrin_llvm_type = llvm_type_of(intrin_type);
#if LLVM_VERSION >= 150
        const bool is_opaque = llvm::PointerType::get(intrin_llvm_type, 0)->isOpaque();
#else
        const bool is_opaque = false;
#endif
        if (target.bits == 32) {
            instr << "llvm.arm.neon.vst"
                  << num_vecs
                  << (is_opaque ? ".p0" : ".p0i8")
                  << ".v"
                  << intrin_type.lanes()
                  << (t.is_float() ? 'f' : 'i')
                  << t.bits();
            arg_types = vector<llvm::Type *>(num_vecs + 2, intrin_llvm_type);
            arg_types.front() = i8_t->getPointerTo();
            arg_types.back() = i32_t;
        } else {
            instr << "llvm.aarch64.neon.st"
                  << num_vecs
                  << ".v"
                  << intrin_type.lanes()
                  << (t.is_float() ? 'f' : 'i')
                  << t.bits()
                  << ".p0";
            if (!is_opaque) {
                instr << (t.is_float() ? 'f' : 'i') << t.bits();
            }
            arg_types = vector<llvm::Type *>(num_vecs + 1, intrin_llvm_type);
            arg_types.back() = llvm_type_of(intrin_type.element_of())->getPointerTo();
        }
        llvm::FunctionType *fn_type = FunctionType::get(llvm::Type::getVoidTy(*context), arg_types, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(instr.str(), fn_type);
        internal_assert(fn);

        // How many vst instructions do we need to generate?
        int slices = t.lanes() / intrin_type.lanes();

        internal_assert(slices >= 1);
        for (int i = 0; i < t.lanes(); i += intrin_type.lanes()) {
            Expr slice_base = simplify(ramp->base + i * num_vecs);
            Expr slice_ramp = Ramp::make(slice_base, ramp->stride, intrin_type.lanes() * num_vecs);
            Value *ptr = codegen_buffer_pointer(op->name, shuffle->vectors[0].type().element_of(), slice_base);

            vector<Value *> slice_args = args;
            // Take a slice of each arg
            for (int j = 0; j < num_vecs; j++) {
                slice_args[j] = slice_vector(slice_args[j], i, intrin_type.lanes());
            }

            if (target.bits == 32) {
                // The arm32 versions take an i8*, regardless of the type stored.
                ptr = builder->CreatePointerCast(ptr, i8_t->getPointerTo());
                // Set the pointer argument
                slice_args.insert(slice_args.begin(), ptr);
                // Set the alignment argument
                slice_args.push_back(ConstantInt::get(i32_t, alignment));
            } else {
                // Set the pointer argument
                slice_args.push_back(ptr);
            }

            CallInst *store = builder->CreateCall(fn, slice_args);
            add_tbaa_metadata(store, op->name, slice_ramp);
        }

        // pop the lets from the symbol table
        for (auto &let : lets) {
            sym_pop(let.first);
        }

        return;
    }

    // If the stride is one or minus one, we can deal with that using vanilla codegen
    const IntImm *stride = ramp->stride.as<IntImm>();
    if (stride && (stride->value == 1 || stride->value == -1)) {
        CodeGen_Posix::visit(op);
        return;
    }

    // We have builtins for strided stores with fixed but unknown stride, but they use inline assembly
    if (target.bits != 64 /* Not yet implemented for aarch64 */) {
        ostringstream builtin;
        builtin << "strided_store_"
                << (op->value.type().is_float() ? "f" : "i")
                << op->value.type().bits()
                << "x" << op->value.type().lanes();

        llvm::Function *fn = module->getFunction(builtin.str());
        if (fn) {
            Value *base = codegen_buffer_pointer(op->name, op->value.type().element_of(), ramp->base);
            Value *stride = codegen(ramp->stride * op->value.type().bytes());
            Value *val = codegen(op->value);
            debug(4) << "Creating call to " << builtin.str() << "\n";
            Value *store_args[] = {base, stride, val};
            Instruction *store = builder->CreateCall(fn, store_args);
            (void)store;
            add_tbaa_metadata(store, op->name, op->index);
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Load *op) {
    // Predicated load
    if (!is_const_one(op->predicate)) {
        CodeGen_Posix::visit(op);
        return;
    }

    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    const Ramp *ramp = op->index.as<Ramp>();

    // We only deal with ramps here
    if (!ramp) {
        CodeGen_Posix::visit(op);
        return;
    }

    // If the stride is in [-1, 4], we can deal with that using vanilla codegen
    const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : nullptr;
    if (stride && (-1 <= stride->value && stride->value <= 4)) {
        CodeGen_Posix::visit(op);
        return;
    }

    // We have builtins for strided loads with fixed but unknown stride, but they use inline assembly.
    if (target.bits != 64 /* Not yet implemented for aarch64 */) {
        ostringstream builtin;
        builtin << "strided_load_"
                << (op->type.is_float() ? "f" : "i")
                << op->type.bits()
                << "x" << op->type.lanes();

        llvm::Function *fn = module->getFunction(builtin.str());
        if (fn) {
            Value *base = codegen_buffer_pointer(op->name, op->type.element_of(), ramp->base);
            Value *stride = codegen(ramp->stride * op->type.bytes());
            debug(4) << "Creating call to " << builtin.str() << "\n";
            Value *args[] = {base, stride};
            Instruction *load = builder->CreateCall(fn, args, builtin.str());
            add_tbaa_metadata(load, op->name, op->index);
            value = load;
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Call *op) {
    if (op->is_intrinsic(Call::sorted_avg)) {
        value = codegen(halving_add(op->args[0], op->args[1]));
        return;
    }

    if (op->is_intrinsic(Call::rounding_shift_right)) {
        // LLVM wants these as rounding_shift_left with a negative b instead.
        Expr b = op->args[1];
        if (!b.type().is_int()) {
            b = Cast::make(b.type().with_code(halide_type_int), b);
        }
        value = codegen(rounding_shift_left(op->args[0], simplify(-b)));
        return;
    } else if (op->is_intrinsic(Call::widening_shift_right) && op->args[1].type().is_int()) {
        // We want these as left shifts with a negative b instead.
        value = codegen(widening_shift_left(op->args[0], simplify(-op->args[1])));
        return;
    } else if (op->is_intrinsic(Call::shift_right) && op->args[1].type().is_int()) {
        // We want these as left shifts with a negative b instead.
        value = codegen(op->args[0] << simplify(-op->args[1]));
        return;
    } else if (op->is_intrinsic(Call::round)) {
        // llvm's roundeven intrinsic reliably lowers to the correct
        // instructions on aarch64, but despite having the same instruction
        // available, it doesn't seem to work for arm-32.
        if (target.bits == 64) {
            value = call_overloaded_intrin(op->type, "round", op->args);
            if (value) {
                return;
            }
        } else if (target.os != Target::Linux) {
            // Furthermore, roundevenf isn't always in the standard library on arm-32
            value = codegen(lower_round_to_nearest_ties_to_even(op->args[0]));
            return;
        }
    }

    if (op->type.is_vector()) {
        vector<Expr> matches;
        for (const Pattern &pattern : calls) {
            if (expr_match(pattern.pattern, op, matches)) {
                if (pattern.intrin.find("shift_right_narrow") != string::npos) {
                    // The shift_right_narrow patterns need the shift to be constant in [1, output_bits].
                    const uint64_t *const_b = as_const_uint(matches[1]);
                    if (!const_b || *const_b == 0 || (int)*const_b > op->type.bits()) {
                        continue;
                    }
                }
                if (target.bits == 32 && pattern.intrin.find("shift_right") != string::npos) {
                    // The 32-bit ARM backend wants right shifts as negative values.
                    matches[1] = simplify(-cast(matches[1].type().with_code(halide_type_int), matches[1]));
                }
                value = call_overloaded_intrin(op->type, pattern.intrin, matches);
                if (value) {
                    return;
                }
            }
        }

        // If we didn't find a pattern, try rewriting any saturating casts.
        static const vector<pair<Expr, Expr>> cast_rewrites = {
            // Double or triple narrowing saturating casts are better expressed as
            // combinations of single narrowing saturating casts.
            {u8_sat(wild_u32x_), u8_sat(u16_sat(wild_u32x_))},
            {u8_sat(wild_i32x_), u8_sat(i16_sat(wild_i32x_))},
            {u8_sat(wild_f32x_), u8_sat(i16_sat(wild_f32x_))},
            {i8_sat(wild_u32x_), i8_sat(u16_sat(wild_u32x_))},
            {i8_sat(wild_i32x_), i8_sat(i16_sat(wild_i32x_))},
            {i8_sat(wild_f32x_), i8_sat(i16_sat(wild_f32x_))},
            {u16_sat(wild_u64x_), u16_sat(u32_sat(wild_u64x_))},
            {u16_sat(wild_i64x_), u16_sat(i32_sat(wild_i64x_))},
            {u16_sat(wild_f64x_), u16_sat(i32_sat(wild_f64x_))},
            {i16_sat(wild_u64x_), i16_sat(u32_sat(wild_u64x_))},
            {i16_sat(wild_i64x_), i16_sat(i32_sat(wild_i64x_))},
            {i16_sat(wild_f64x_), i16_sat(i32_sat(wild_f64x_))},
            {u8_sat(wild_u64x_), u8_sat(u16_sat(u32_sat(wild_u64x_)))},
            {u8_sat(wild_i64x_), u8_sat(i16_sat(i32_sat(wild_i64x_)))},
            {u8_sat(wild_f64x_), u8_sat(i16_sat(i32_sat(wild_f64x_)))},
            {i8_sat(wild_u64x_), i8_sat(u16_sat(u32_sat(wild_u64x_)))},
            {i8_sat(wild_i64x_), i8_sat(i16_sat(i32_sat(wild_i64x_)))},
            {i8_sat(wild_f64x_), i8_sat(i16_sat(i32_sat(wild_f64x_)))},
        };
        for (const auto &i : cast_rewrites) {
            if (expr_match(i.first, op, matches)) {
                Expr replacement = substitute("*", matches[0], with_lanes(i.second, op->type.lanes()));
                debug(3) << "rewriting cast to: " << replacement << " from " << Expr(op) << "\n";
                value = codegen(replacement);
                return;
            }
        }
    }

    if (target.has_feature(Target::ARMFp16)) {
        auto it = float16_transcendental_remapping.find(op->name);
        if (it != float16_transcendental_remapping.end()) {
            // This op doesn't have float16 native function.
            // So we call float32 equivalent func with native type conversion between fp16 and fp32
            // instead of emulated equivalent code as in EmulatedFloat16Math.cpp
            std::vector<Expr> new_args(op->args.size());
            for (size_t i = 0; i < op->args.size(); i++) {
                new_args[i] = cast(Float(32, op->args[i].type().lanes()), op->args[i]);
            }
            const auto &fp32_func_name = it->second;
            Expr e = Call::make(Float(32, op->type.lanes()), fp32_func_name, new_args, op->call_type,
                                op->func, op->value_index, op->image, op->param);
            value = codegen(cast(Float(16, e.type().lanes()), e));
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const LT *op) {
    if (op->a.type().is_float() && op->type.is_vector()) {
        // Fast-math flags confuse LLVM's aarch64 backend, so
        // temporarily clear them for this instruction.
        // See https://bugs.llvm.org/show_bug.cgi?id=45036
        llvm::IRBuilderBase::FastMathFlagGuard guard(*builder);
        builder->clearFastMathFlags();
        CodeGen_Posix::visit(op);
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const LE *op) {
    if (op->a.type().is_float() && op->type.is_vector()) {
        // Fast-math flags confuse LLVM's aarch64 backend, so
        // temporarily clear them for this instruction.
        // See https://bugs.llvm.org/show_bug.cgi?id=45036
        llvm::IRBuilderBase::FastMathFlagGuard guard(*builder);
        builder->clearFastMathFlags();
        CodeGen_Posix::visit(op);
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::codegen_vector_reduce(const VectorReduce *op, const Expr &init) {
    if (neon_intrinsics_disabled() ||
        op->op == VectorReduce::Or ||
        op->op == VectorReduce::And ||
        op->op == VectorReduce::Mul) {
        CodeGen_Posix::codegen_vector_reduce(op, init);
        return;
    }

    struct Pattern {
        VectorReduce::Operator reduce_op;
        int factor;
        Expr pattern;
        const char *intrin;
        Target::Feature required_feature;
        std::vector<int> extra_operands;
    };
    // clang-format off
    static const Pattern patterns[] = {
        {VectorReduce::Add, 4, i32(widening_mul(wild_i8x_, wild_i8x_)), "dot_product", Target::ARMDotProd},
        {VectorReduce::Add, 4, i32(widening_mul(wild_u8x_, wild_u8x_)), "dot_product", Target::ARMDotProd},
        {VectorReduce::Add, 4, u32(widening_mul(wild_u8x_, wild_u8x_)), "dot_product", Target::ARMDotProd},
        // A sum is the same as a dot product with a vector of ones, and this appears to
        // be a bit faster.
        {VectorReduce::Add, 4, i32(wild_i8x_), "dot_product", Target::ARMDotProd, {1}},
        {VectorReduce::Add, 4, i32(wild_u8x_), "dot_product", Target::ARMDotProd, {1}},
        {VectorReduce::Add, 4, u32(wild_u8x_), "dot_product", Target::ARMDotProd, {1}},
    };
    // clang-format on

    int factor = op->value.type().lanes() / op->type.lanes();
    vector<Expr> matches;
    for (const Pattern &p : patterns) {
        if (op->op != p.reduce_op || factor % p.factor != 0) {
            continue;
        }
        if (!target.has_feature(p.required_feature)) {
            continue;
        }
        if (expr_match(p.pattern, op->value, matches)) {
            if (factor != p.factor) {
                Expr equiv = VectorReduce::make(op->op, op->value, op->value.type().lanes() / p.factor);
                equiv = VectorReduce::make(op->op, equiv, op->type.lanes());
                codegen_vector_reduce(equiv.as<VectorReduce>(), init);
                return;
            }

            for (int i : p.extra_operands) {
                matches.push_back(make_const(matches[0].type(), i));
            }

            Expr i = init;
            if (!i.defined()) {
                i = make_zero(op->type);
            }
            if (const Shuffle *s = matches[0].as<Shuffle>()) {
                if (s->is_broadcast()) {
                    // LLVM wants the broadcast as the second operand for the broadcasting
                    // variant of udot/sdot.
                    std::swap(matches[0], matches[1]);
                }
            }
            value = call_overloaded_intrin(op->type, p.intrin, {i, matches[0], matches[1]});
            if (value) {
                return;
            }
        }
    }

    // TODO: Move this to be patterns? The patterns are pretty trivial, but some
    // of the other logic is tricky.
    const char *intrin = nullptr;
    vector<Expr> intrin_args;
    Expr accumulator = init;
    if (op->op == VectorReduce::Add && factor == 2) {
        Type narrow_type = op->type.narrow().with_lanes(op->value.type().lanes());
        Expr narrow = lossless_cast(narrow_type, op->value);
        if (!narrow.defined() && op->type.is_int()) {
            // We can also safely accumulate from a uint into a
            // wider int, because the addition uses at most one
            // extra bit.
            narrow = lossless_cast(narrow_type.with_code(Type::UInt), op->value);
        }
        if (narrow.defined()) {
            if (init.defined() && target.bits == 32) {
                // On 32-bit, we have an intrinsic for widening add-accumulate.
                // TODO: this could be written as a pattern with widen_right_add (#6951).
                intrin = "pairwise_widening_add_accumulate";
                intrin_args = {accumulator, narrow};
                accumulator = Expr();
            } else {
                // On 64-bit, LLVM pattern matches widening add-accumulate if
                // we give it the widening add.
                intrin = "pairwise_widening_add";
                intrin_args = {narrow};
            }
        } else {
            intrin = "pairwise_add";
            intrin_args = {op->value};
        }
    } else if (op->op == VectorReduce::Min && factor == 2) {
        intrin = "pairwise_min";
        intrin_args = {op->value};
    } else if (op->op == VectorReduce::Max && factor == 2) {
        intrin = "pairwise_max";
        intrin_args = {op->value};
    }

    if (intrin) {
        value = call_overloaded_intrin(op->type, intrin, intrin_args);
        if (value) {
            if (accumulator.defined()) {
                // We still have an initial value to take care of
                string n = unique_name('t');
                sym_push(n, value);
                Expr v = Variable::make(accumulator.type(), n);
                switch (op->op) {
                case VectorReduce::Add:
                    accumulator += v;
                    break;
                case VectorReduce::Min:
                    accumulator = min(accumulator, v);
                    break;
                case VectorReduce::Max:
                    accumulator = max(accumulator, v);
                    break;
                default:
                    internal_error << "unreachable";
                }
                codegen(accumulator);
                sym_pop(n);
            }
            return;
        }
    }

    CodeGen_Posix::codegen_vector_reduce(op, init);
}

Type CodeGen_ARM::upgrade_type_for_arithmetic(const Type &t) const {
    if (is_float16_and_has_feature(t)) {
        return t;
    }
    return CodeGen_Posix::upgrade_type_for_arithmetic(t);
}

Type CodeGen_ARM::upgrade_type_for_argument_passing(const Type &t) const {
    if (is_float16_and_has_feature(t)) {
        return t;
    }
    return CodeGen_Posix::upgrade_type_for_argument_passing(t);
}

Type CodeGen_ARM::upgrade_type_for_storage(const Type &t) const {
    if (is_float16_and_has_feature(t)) {
        return t;
    }
    return CodeGen_Posix::upgrade_type_for_storage(t);
}

string CodeGen_ARM::mcpu_target() const {
    if (target.bits == 32) {
        if (target.has_feature(Target::ARMv7s)) {
            return "swift";
        } else {
            return "cortex-a9";
        }
    } else {
        if (target.os == Target::IOS) {
            return "cyclone";
        } else if (target.os == Target::OSX) {
            return "apple-a12";
        } else {
            return "generic";
        }
    }
}

string CodeGen_ARM::mcpu_tune() const {
    return mcpu_target();
}

string CodeGen_ARM::mattrs() const {
    if (target.bits == 32) {
        if (target.has_feature(Target::ARMv7s)) {
            return "+neon";
        }
        if (!target.has_feature(Target::NoNEON)) {
            return "+neon";
        } else {
            return "-neon";
        }
    } else {
        // TODO: Should Halide's SVE flags be 64-bit only?
        string arch_flags;
        string separator;
        if (target.has_feature(Target::SVE2)) {
            arch_flags = "+sve2";
            separator = ",";
        } else if (target.has_feature(Target::SVE)) {
            arch_flags = "+sve";
            separator = ",";
        }

        if (target.has_feature(Target::ARMv81a)) {
            arch_flags += separator + "+v8.1a";
            separator = ",";
        }

        if (target.has_feature(Target::ARMDotProd)) {
            arch_flags += separator + "+dotprod";
            separator = ",";
        }

        if (target.has_feature(Target::ARMFp16)) {
            arch_flags += separator + "+fullfp16";
            separator = ",";
        }

        if (target.os == Target::IOS || target.os == Target::OSX) {
            return arch_flags + separator + "+reserve-x18";
        } else {
            return arch_flags;
        }
    }
}

bool CodeGen_ARM::use_soft_float_abi() const {
    // One expects the flag is irrelevant on 64-bit, but we'll make the logic
    // exhaustive anyway. It is not clear the armv7s case is necessary either.
    return target.has_feature(Target::SoftFloatABI) ||
           (target.bits == 32 &&
            ((target.os == Target::Android) ||
             (target.os == Target::IOS && !target.has_feature(Target::ARMv7s))));
}

int CodeGen_ARM::native_vector_bits() const {
    return 128;
}

bool CodeGen_ARM::supports_call_as_float16(const Call *op) const {
    bool is_fp16_native = float16_native_funcs.find(op->name) != float16_native_funcs.end();
    bool is_fp16_transcendental = float16_transcendental_remapping.find(op->name) != float16_transcendental_remapping.end();
    return target.has_feature(Target::ARMFp16) && (is_fp16_native || is_fp16_transcendental);
}

}  // namespace

std::unique_ptr<CodeGen_Posix> new_CodeGen_ARM(const Target &target) {
    return std::make_unique<CodeGen_ARM>(target);
}

#else  // WITH_ARM || WITH_AARCH64

std::unique_ptr<CodeGen_Posix> new_CodeGen_ARM(const Target &target) {
    user_error << "ARM not enabled for this build of Halide.\n";
    return nullptr;
}

#endif  // WITH_ARM || WITH_AARCH64

}  // namespace Internal
}  // namespace Halide
