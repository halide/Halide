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
    using codegen_func_t = std::function<Value *(int lanes, const std::vector<Value *> &)>;

    int target_vscale() const override;

    /** Define a wrapper LLVM func that takes some arguments which Halide defines
     * and call inner LLVM intrinsic with an additional argument which LLVM requires. */
    llvm::Function *define_intrin_wrapper(const std::string &inner_name,
                                          const Type &ret_type,
                                          const std::string &mangled_name,
                                          const std::vector<Type> &arg_types,
                                          bool add_inactive_arg,  // for SVE
                                          bool add_predicate,     // for SVE
                                          bool split_arg0,
                                          bool scalars_are_vectors);

    void init_module() override;
    void begin_func(LinkageType linkage, const std::string &simple_name,
                    const std::string &extern_name, const std::vector<LoweredArgument> &args) override;

    /** Nodes for which we want to emit specific neon intrinsics */
    // @{
    void visit(const Cast *) override;
    void visit(const Add *) override;
    void visit(const Sub *) override;
    void visit(const Mul *) override;
    void visit(const Div *) override;
    void visit(const Min *) override;
    void visit(const Max *) override;
    void visit(const Store *) override;
    void visit(const Load *) override;
    void visit(const Ramp *op) override;
    void visit(const Call *) override;
    void visit(const EQ *) override;
    void visit(const NE *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void visit(const GT *) override;
    void visit(const GE *) override;
    void visit(const And *) override;
    void visit(const Or *) override;
    void visit(const Not *) override;
    void visit(const Select *op) override;
    void visit(const Shuffle *) override;
    void codegen_vector_reduce(const VectorReduce *, const Expr &) override;
    bool codegen_dot_product_vector_reduce(const VectorReduce *, const Expr &);
    bool codegen_pairwise_vector_reduce(const VectorReduce *, const Expr &);
    bool codegen_across_vector_reduce(const VectorReduce *, const Expr &);
    // @}
    Type upgrade_type_for_arithmetic(const Type &t) const override;
    Type upgrade_type_for_argument_passing(const Type &t) const override;
    Type upgrade_type_for_storage(const Type &t) const override;
    Value *interleave_vectors(const std::vector<Value *> &) override;
    Value *shuffle_vectors(Value *a, Value *b, const std::vector<int> &indices) override;
    Value *shuffle_single_vector_with_tbl(Value *a, const std::vector<int> &indices);
    Value *shuffle_vectors_with_tbl2(Value *a, Value *b, const std::vector<int> &indices);
    Value *try_to_decompose_into_sub_shuffles(Value *a, Value *b, const std::vector<int> &indices);
    Value *codegen_shuffle_indices(int bits, const std::vector<int> &indices);
    Value *codegen_whilelt(int total_lanes, int start, int end);

    /** Helper function to perform codegen in native vector lanes basis.
     * This API is mainly used in case LLVM error occurs with vanila codegen for
     * vector type with "unnatural" lanes.
     * The condition is checked to see if it is necessary to slice the op.
     * In case vanila codegen is possible to handle, nothing happens and false is returned.
     */
    bool codegen_with_natural_lanes_if_necessary(Type op_type, const std::vector<Expr> &args, codegen_func_t &cg_func);

    /** Helper function to perform codegen of vector operation in a way that
     * total_lanes are divided into slices, codegen is performed for each slice
     * and results are concatenated into total_lanes.
     */
    Value *codegen_with_lanes(int slice_lanes, int total_lanes, const std::vector<Expr> &args, codegen_func_t &cg_func);

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

    template<typename T>
    T align_up(T x, int n) const {
        return (x + n - 1) / n * n;
    }

    /** Make predicate vector which starts with consecutive true followed by consecutive false */
    Expr make_vector_predicate_1s_0s(int true_lanes, int false_lanes) {
        return Shuffle::make_concat({const_true(true_lanes), const_false(false_lanes)});
    }
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
    casts.emplace_back("saturating_rounding_shift_right_narrow", i8_sat(rounding_shift_right(wild_i16x_, wild_u16_)));
    casts.emplace_back("saturating_rounding_shift_right_narrow", u8_sat(rounding_shift_right(wild_u16x_, wild_u16_)));
    casts.emplace_back("saturating_rounding_shift_right_narrow", u8_sat(rounding_shift_right(wild_i16x_, wild_u16_)));
    casts.emplace_back("saturating_rounding_shift_right_narrow", i16_sat(rounding_shift_right(wild_i32x_, wild_u32_)));
    casts.emplace_back("saturating_rounding_shift_right_narrow", u16_sat(rounding_shift_right(wild_u32x_, wild_u32_)));
    casts.emplace_back("saturating_rounding_shift_right_narrow", u16_sat(rounding_shift_right(wild_i32x_, wild_u32_)));
    casts.emplace_back("saturating_rounding_shift_right_narrow", i32_sat(rounding_shift_right(wild_i64x_, wild_u64_)));
    casts.emplace_back("saturating_rounding_shift_right_narrow", u32_sat(rounding_shift_right(wild_u64x_, wild_u64_)));
    casts.emplace_back("saturating_rounding_shift_right_narrow", u32_sat(rounding_shift_right(wild_i64x_, wild_u64_)));

    // SQSHL, UQSHL, SQSHLU - Saturating shift left by signed register.
    for (const Expr &rhs : {wild_i8x_, wild_u8x_}) {
        casts.emplace_back("saturating_shift_left", i8_sat(widening_shift_left(wild_i8x_, rhs)));
        casts.emplace_back("saturating_shift_left", u8_sat(widening_shift_left(wild_u8x_, rhs)));
        casts.emplace_back("saturating_shift_left", u8_sat(widening_shift_left(wild_i8x_, rhs)));
    }
    for (const Expr &rhs : {wild_i16x_, wild_u16x_}) {
        casts.emplace_back("saturating_shift_left", i16_sat(widening_shift_left(wild_i16x_, rhs)));
        casts.emplace_back("saturating_shift_left", u16_sat(widening_shift_left(wild_u16x_, rhs)));
        casts.emplace_back("saturating_shift_left", u16_sat(widening_shift_left(wild_i16x_, rhs)));
    }
    for (const Expr &rhs : {wild_i32x_, wild_u32x_}) {
        casts.emplace_back("saturating_shift_left", i32_sat(widening_shift_left(wild_i32x_, rhs)));
        casts.emplace_back("saturating_shift_left", u32_sat(widening_shift_left(wild_u32x_, rhs)));
        casts.emplace_back("saturating_shift_left", u32_sat(widening_shift_left(wild_i32x_, rhs)));
    }

    // SQSHRN, UQSHRN, SQRSHRUN Saturating narrowing shift right by an (by immediate in [1, output bits])
    casts.emplace_back("saturating_shift_right_narrow", i8_sat(wild_i16x_ >> wild_u16_));
    casts.emplace_back("saturating_shift_right_narrow", u8_sat(wild_u16x_ >> wild_u16_));
    casts.emplace_back("saturating_shift_right_narrow", u8_sat(wild_i16x_ >> wild_u16_));
    casts.emplace_back("saturating_shift_right_narrow", i16_sat(wild_i32x_ >> wild_u32_));
    casts.emplace_back("saturating_shift_right_narrow", u16_sat(wild_u32x_ >> wild_u32_));
    casts.emplace_back("saturating_shift_right_narrow", u16_sat(wild_i32x_ >> wild_u32_));
    casts.emplace_back("saturating_shift_right_narrow", i32_sat(wild_i64x_ >> wild_u64_));
    casts.emplace_back("saturating_shift_right_narrow", u32_sat(wild_u64x_ >> wild_u64_));
    casts.emplace_back("saturating_shift_right_narrow", u32_sat(wild_i64x_ >> wild_u64_));

    // SRSHL, URSHL - Rounding shift left (by signed vector)
    // These are already written as rounding_shift_left

    // SRSHR, URSHR - Rounding shift right (by immediate in [1, output bits])
    // These patterns are almost identity, we just need to strip off the broadcast.

    // SSHLL, USHLL - Shift left long (by immediate in [0, output bits - 1])
    // These patterns are almost identity, we just need to strip off the broadcast.

    // SQXTN, UQXTN, SQXTUN - Saturating narrow.
    casts.emplace_back("saturating_narrow", i8_sat(wild_i16x_));
    casts.emplace_back("saturating_narrow", u8_sat(wild_u16x_));
    casts.emplace_back("saturating_narrow", u8_sat(wild_i16x_));
    casts.emplace_back("saturating_narrow", i16_sat(wild_i32x_));
    casts.emplace_back("saturating_narrow", u16_sat(wild_u32x_));
    casts.emplace_back("saturating_narrow", u16_sat(wild_i32x_));
    casts.emplace_back("saturating_narrow", i32_sat(wild_i64x_));
    casts.emplace_back("saturating_narrow", u32_sat(wild_u64x_));
    casts.emplace_back("saturating_narrow", u32_sat(wild_i64x_));

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
        AllowUnsignedOp1 = 1 << 0,    // Generate a second version of the instruction with the second operand unsigned.
        HalfWidth = 1 << 1,           // This is a half-width instruction that should have a full width version generated as well.
        NoMangle = 1 << 2,            // Don't mangle this intrinsic name.
        MangleArgs = 1 << 3,          // Most intrinsics only mangle the return type. Some mangle the arguments instead.
        MangleRetArgs = 1 << 4,       // Most intrinsics only mangle the return type. Some mangle the return type and arguments instead.
        MangleRetArg1 = 1 << 5,       // Most intrinsics only mangle the return type. Some mangle the return type and arguments[1] instead.
        ScalarsAreVectors = 1 << 6,   // Some intrinsics have scalar arguments that are vector parameters :(
        SplitArg0 = 1 << 7,           // This intrinsic requires splitting the argument into the low and high halves.
        NoPrefix = 1 << 8,            // Don't prefix the intrinsic with llvm.*
        RequireFp16 = 1 << 9,         // Available only if Target has ARMFp16 feature
        Neon64Unavailable = 1 << 10,  // Unavailalbe for 64 bit NEON
        SveUnavailable = 1 << 11,     // Unavailable for SVE
        SveNoPredicate = 1 << 12,     // In SVE intrinsics, additional predicate argument is required as default, unless this flag is set.
        SveInactiveArg = 1 << 13,     // This intrinsic needs the additional argument for fallback value for the lanes inactivated by predicate.
    };
};

// clang-format off
const ArmIntrinsic intrinsic_defs[] = {
    // NOTE about SVE2 intrinsics:
    // Some of the Arm intrinsic have the same name between Neon and SVE2 but with different behavior. For example,
    // widening, narrowing and pair-wise operations which are performed in even (top) and odd (bottom) lanes basis in SVE,
    // while in high and low lanes in Neon. Therefore, peep-hole code-gen with those SVE2 intrinsic is not enabled for now,
    // because additional interleaving/deinterleaveing would be required to restore the element order in a vector.

    {"vabs", "abs", UInt(8, 8), "abs", {Int(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveInactiveArg},
    {"vabs", "abs", UInt(16, 4), "abs", {Int(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveInactiveArg},
    {"vabs", "abs", UInt(32, 2), "abs", {Int(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveInactiveArg},
    {"llvm.fabs", "llvm.fabs", Float(16, 4), "abs", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16},
    {"llvm.fabs", "llvm.fabs", Float(32, 2), "abs", {Float(32, 2)}, ArmIntrinsic::HalfWidth},
    {"llvm.fabs", "llvm.fabs", Float(64, 2), "abs", {Float(64, 2)}},

    {"llvm.sqrt", "llvm.sqrt", Float(16, 4), "sqrt_f16", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16},
    {"llvm.sqrt", "llvm.sqrt", Float(32, 2), "sqrt_f32", {Float(32, 2)}, ArmIntrinsic::HalfWidth},
    {"llvm.sqrt", "llvm.sqrt", Float(64, 2), "sqrt_f64", {Float(64, 2)}},

    {"llvm.floor", "llvm.floor", Float(16, 4), "floor_f16", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16},
    {"llvm.floor", "llvm.floor", Float(32, 2), "floor_f32", {Float(32, 2)}, ArmIntrinsic::HalfWidth},
    {"llvm.floor", "llvm.floor", Float(64, 2), "floor_f64", {Float(64, 2)}},

    {"llvm.ceil", "llvm.ceil", Float(16, 4), "ceil_f16", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16},
    {"llvm.ceil", "llvm.ceil", Float(32, 2), "ceil_f32", {Float(32, 2)}, ArmIntrinsic::HalfWidth},
    {"llvm.ceil", "llvm.ceil", Float(64, 2), "ceil_f64", {Float(64, 2)}},

    {"llvm.round", "llvm.nearbyint", Float(16, 4), "round_f16", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16},
    {"llvm.round", "llvm.nearbyint", Float(32, 2), "round_f32", {Float(32, 2)}, ArmIntrinsic::HalfWidth},
    {"llvm.round", "llvm.nearbyint", Float(64, 2), "round_f64", {Float(64, 2)}},

    {"llvm.trunc", "llvm.trunc", Float(16, 4), "trunc_f16", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16},
    {"llvm.trunc", "llvm.trunc", Float(32, 2), "trunc_f32", {Float(32, 2)}, ArmIntrinsic::HalfWidth},
    {"llvm.trunc", "llvm.trunc", Float(64, 2), "trunc_f64", {Float(64, 2)}},

    // SABD, UABD - Absolute difference
    {"vabds", "sabd", UInt(8, 8), "absd", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vabdu", "uabd", UInt(8, 8), "absd", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vabds", "sabd", UInt(16, 4), "absd", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vabdu", "uabd", UInt(16, 4), "absd", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vabds", "sabd", UInt(32, 2), "absd", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vabdu", "uabd", UInt(32, 2), "absd", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::HalfWidth},

    // SMULL, UMULL - Widening multiply
    {"vmulls", "smull", Int(16, 8), "widening_mul", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::SveUnavailable},
    {"vmullu", "umull", UInt(16, 8), "widening_mul", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::SveUnavailable},
    {"vmulls", "smull", Int(32, 4), "widening_mul", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::SveUnavailable},
    {"vmullu", "umull", UInt(32, 4), "widening_mul", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::SveUnavailable},
    {"vmulls", "smull", Int(64, 2), "widening_mul", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::SveUnavailable},
    {"vmullu", "umull", UInt(64, 2), "widening_mul", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::SveUnavailable},

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
    {nullptr, "smin", Int(64, 2), "min", {Int(64, 2), Int(64, 2)}, ArmIntrinsic::Neon64Unavailable},
    {nullptr, "umin", UInt(64, 2), "min", {UInt(64, 2), UInt(64, 2)}, ArmIntrinsic::Neon64Unavailable},
    {"vmins", "fmin", Float(16, 4), "min", {Float(16, 4), Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16},
    {"vmins", "fmin", Float(32, 2), "min", {Float(32, 2), Float(32, 2)}, ArmIntrinsic::HalfWidth},
    {nullptr, "fmin", Float(64, 2), "min", {Float(64, 2), Float(64, 2)}},

    // FCVTZS, FCVTZU
    {nullptr, "fcvtzs", Int(16, 4), "fp_to_int", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::RequireFp16 | ArmIntrinsic::SveInactiveArg},
    {nullptr, "fcvtzu", UInt(16, 4), "fp_to_int", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::RequireFp16 | ArmIntrinsic::SveInactiveArg},
    {nullptr, "fcvtzs", Int(32, 2), "fp_to_int", {Float(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveInactiveArg},
    {nullptr, "fcvtzu", UInt(32, 2), "fp_to_int", {Float(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveInactiveArg},
    {nullptr, "fcvtzs", Int(64, 2), "fp_to_int", {Float(64, 2)}, ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveInactiveArg},
    {nullptr, "fcvtzu", UInt(64, 2), "fp_to_int", {Float(64, 2)}, ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveInactiveArg},

    // SMAX, UMAX, FMAX - Max
    {"vmaxs", "smax", Int(8, 8), "max", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vmaxu", "umax", UInt(8, 8), "max", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::HalfWidth},
    {"vmaxs", "smax", Int(16, 4), "max", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vmaxu", "umax", UInt(16, 4), "max", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::HalfWidth},
    {"vmaxs", "smax", Int(32, 2), "max", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth},
    {"vmaxu", "umax", UInt(32, 2), "max", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::HalfWidth},
    {nullptr, "smax", Int(64, 2), "max", {Int(64, 2), Int(64, 2)}, ArmIntrinsic::Neon64Unavailable},
    {nullptr, "umax", UInt(64, 2), "max", {UInt(64, 2), UInt(64, 2)}, ArmIntrinsic::Neon64Unavailable},
    {"vmaxs", "fmax", Float(16, 4), "max", {Float(16, 4), Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16},
    {"vmaxs", "fmax", Float(32, 2), "max", {Float(32, 2), Float(32, 2)}, ArmIntrinsic::HalfWidth},
    {nullptr, "fmax", Float(64, 2), "max", {Float(64, 2), Float(64, 2)}},

    // NEG, FNEG
    {nullptr, "neg", Int(8, 16), "negate", {Int(8, 16)}, ArmIntrinsic::SveInactiveArg | ArmIntrinsic::Neon64Unavailable},
    {nullptr, "neg", Int(16, 8), "negate", {Int(16, 8)}, ArmIntrinsic::SveInactiveArg | ArmIntrinsic::Neon64Unavailable},
    {nullptr, "neg", Int(32, 4), "negate", {Int(32, 4)}, ArmIntrinsic::SveInactiveArg | ArmIntrinsic::Neon64Unavailable},
    {nullptr, "neg", Int(64, 2), "negate", {Int(64, 2)}, ArmIntrinsic::SveInactiveArg | ArmIntrinsic::Neon64Unavailable},
    {nullptr, "fneg", Float(16, 8), "negate", {Float(16, 8)}, ArmIntrinsic::SveInactiveArg | ArmIntrinsic::Neon64Unavailable},
    {nullptr, "fneg", Float(32, 4), "negate", {Float(32, 4)}, ArmIntrinsic::SveInactiveArg | ArmIntrinsic::Neon64Unavailable},
    {nullptr, "fneg", Float(64, 2), "negate", {Float(64, 2)}, ArmIntrinsic::SveInactiveArg | ArmIntrinsic::Neon64Unavailable},

    // SQNEG, UQNEG - Saturating negation
    {"vqneg", "sqneg", Int(8, 8), "saturating_negate", {Int(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveInactiveArg},
    {"vqneg", "sqneg", Int(16, 4), "saturating_negate", {Int(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveInactiveArg},
    {"vqneg", "sqneg", Int(32, 2), "saturating_negate", {Int(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveInactiveArg},
    {"vqneg", "sqneg", Int(64, 2), "saturating_negate", {Int(64, 2)}, ArmIntrinsic::SveInactiveArg},

    // SQXTN, UQXTN, SQXTUN - Saturating narrowing
    {"vqmovns", "sqxtn", Int(8, 8), "saturating_narrow", {Int(16, 8)}, ArmIntrinsic::SveUnavailable},
    {"vqmovnu", "uqxtn", UInt(8, 8), "saturating_narrow", {UInt(16, 8)}, ArmIntrinsic::SveUnavailable},
    {"vqmovnsu", "sqxtun", UInt(8, 8), "saturating_narrow", {Int(16, 8)}, ArmIntrinsic::SveUnavailable},
    {"vqmovns", "sqxtn", Int(16, 4), "saturating_narrow", {Int(32, 4)}, ArmIntrinsic::SveUnavailable},
    {"vqmovnu", "uqxtn", UInt(16, 4), "saturating_narrow", {UInt(32, 4)}, ArmIntrinsic::SveUnavailable},
    {"vqmovnsu", "sqxtun", UInt(16, 4), "saturating_narrow", {Int(32, 4)}, ArmIntrinsic::SveUnavailable},
    {"vqmovns", "sqxtn", Int(32, 2), "saturating_narrow", {Int(64, 2)}, ArmIntrinsic::SveUnavailable},
    {"vqmovnu", "uqxtn", UInt(32, 2), "saturating_narrow", {UInt(64, 2)}, ArmIntrinsic::SveUnavailable},
    {"vqmovnsu", "sqxtun", UInt(32, 2), "saturating_narrow", {Int(64, 2)}, ArmIntrinsic::SveUnavailable},

    // RSHRN - Rounding shift right narrow (by immediate in [1, output bits])
    // arm32 expects a vector RHS of the same type as the LHS except signed.
    {"vrshiftn", nullptr, Int(8, 8), "rounding_shift_right_narrow", {Int(16, 8), Int(16, 8)}},
    {"vrshiftn", nullptr, UInt(8, 8), "rounding_shift_right_narrow", {UInt(16, 8), Int(16, 8)}},
    {"vrshiftn", nullptr, Int(16, 4), "rounding_shift_right_narrow", {Int(32, 4), Int(32, 4)}},
    {"vrshiftn", nullptr, UInt(16, 4), "rounding_shift_right_narrow", {UInt(32, 4), Int(32, 4)}},
    {"vrshiftn", nullptr, Int(32, 2), "rounding_shift_right_narrow", {Int(64, 2), Int(64, 2)}},
    {"vrshiftn", nullptr, UInt(32, 2), "rounding_shift_right_narrow", {UInt(64, 2), Int(64, 2)}},

    // arm64 expects a 32-bit constant.
    {nullptr, "rshrn", Int(8, 8), "rounding_shift_right_narrow", {Int(16, 8), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "rshrn", UInt(8, 8), "rounding_shift_right_narrow", {UInt(16, 8), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "rshrn", Int(16, 4), "rounding_shift_right_narrow", {Int(32, 4), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "rshrn", UInt(16, 4), "rounding_shift_right_narrow", {UInt(32, 4), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "rshrn", Int(32, 2), "rounding_shift_right_narrow", {Int(64, 2), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "rshrn", UInt(32, 2), "rounding_shift_right_narrow", {UInt(64, 2), UInt(32)}, ArmIntrinsic::SveUnavailable},

    // SHRN - Shift right narrow (by immediate in [1, output bits])
    // LLVM pattern matches these.

    // SQRSHRN, UQRSHRN, SQRSHRUN - Saturating rounding narrowing shift right (by immediate in [1, output bits])
    // arm32 expects a vector RHS of the same type as the LHS except signed.
    {"vqrshiftns", nullptr, Int(8, 8), "saturating_rounding_shift_right_narrow", {Int(16, 8), Int(16, 8)}, ArmIntrinsic::SveUnavailable},
    {"vqrshiftnu", nullptr, UInt(8, 8), "saturating_rounding_shift_right_narrow", {UInt(16, 8), Int(16, 8)}, ArmIntrinsic::SveUnavailable},
    {"vqrshiftnsu", nullptr, UInt(8, 8), "saturating_rounding_shift_right_narrow", {Int(16, 8), Int(16, 8)}, ArmIntrinsic::SveUnavailable},
    {"vqrshiftns", nullptr, Int(16, 4), "saturating_rounding_shift_right_narrow", {Int(32, 4), Int(32, 4)}, ArmIntrinsic::SveUnavailable},
    {"vqrshiftnu", nullptr, UInt(16, 4), "saturating_rounding_shift_right_narrow", {UInt(32, 4), Int(32, 4)}, ArmIntrinsic::SveUnavailable},
    {"vqrshiftnsu", nullptr, UInt(16, 4), "saturating_rounding_shift_right_narrow", {Int(32, 4), Int(32, 4)}, ArmIntrinsic::SveUnavailable},
    {"vqrshiftns", nullptr, Int(32, 2), "saturating_rounding_shift_right_narrow", {Int(64, 2), Int(64, 2)}, ArmIntrinsic::SveUnavailable},
    {"vqrshiftnu", nullptr, UInt(32, 2), "saturating_rounding_shift_right_narrow", {UInt(64, 2), Int(64, 2)}, ArmIntrinsic::SveUnavailable},
    {"vqrshiftnsu", nullptr, UInt(32, 2), "saturating_rounding_shift_right_narrow", {Int(64, 2), Int(64, 2)}, ArmIntrinsic::SveUnavailable},

    // arm64 expects a 32-bit constant.
    {nullptr, "sqrshrn", Int(8, 8), "saturating_rounding_shift_right_narrow", {Int(16, 8), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "uqrshrn", UInt(8, 8), "saturating_rounding_shift_right_narrow", {UInt(16, 8), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "sqrshrun", UInt(8, 8), "saturating_rounding_shift_right_narrow", {Int(16, 8), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "sqrshrn", Int(16, 4), "saturating_rounding_shift_right_narrow", {Int(32, 4), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "uqrshrn", UInt(16, 4), "saturating_rounding_shift_right_narrow", {UInt(32, 4), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "sqrshrun", UInt(16, 4), "saturating_rounding_shift_right_narrow", {Int(32, 4), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "sqrshrn", Int(32, 2), "saturating_rounding_shift_right_narrow", {Int(64, 2), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "uqrshrn", UInt(32, 2), "saturating_rounding_shift_right_narrow", {UInt(64, 2), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "sqrshrun", UInt(32, 2), "saturating_rounding_shift_right_narrow", {Int(64, 2), UInt(32)}, ArmIntrinsic::SveUnavailable},

    // SQSHL, UQSHL, SQSHLU - Saturating shift left by signed register.
    // There is also an immediate version of this - hopefully LLVM does this matching when appropriate.
    {"vqshifts", "sqshl", Int(8, 8), "saturating_shift_left", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshiftu", "uqshl", UInt(8, 8), "saturating_shift_left", {UInt(8, 8), Int(8, 8)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshiftsu", "sqshlu", UInt(8, 8), "saturating_shift_left", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {"vqshifts", "sqshl", Int(16, 4), "saturating_shift_left", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshiftu", "uqshl", UInt(16, 4), "saturating_shift_left", {UInt(16, 4), Int(16, 4)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshiftsu", "sqshlu", UInt(16, 4), "saturating_shift_left", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {"vqshifts", "sqshl", Int(32, 2), "saturating_shift_left", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshiftu", "uqshl", UInt(32, 2), "saturating_shift_left", {UInt(32, 2), Int(32, 2)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth},
    {"vqshiftsu", "sqshlu", UInt(32, 2), "saturating_shift_left", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {"vqshifts", "sqshl", Int(64, 2), "saturating_shift_left", {Int(64, 2), Int(64, 2)}, ArmIntrinsic::AllowUnsignedOp1},
    {"vqshiftu", "uqshl", UInt(64, 2), "saturating_shift_left", {UInt(64, 2), Int(64, 2)}, ArmIntrinsic::AllowUnsignedOp1},
    {"vqshiftsu", "sqshlu", UInt(64, 2), "saturating_shift_left", {Int(64, 2), Int(64, 2)}, ArmIntrinsic::AllowUnsignedOp1 | ArmIntrinsic::SveUnavailable},

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
    {nullptr, "sqshrn", Int(8, 8), "saturating_shift_right_narrow", {Int(16, 8), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "uqshrn", UInt(8, 8), "saturating_shift_right_narrow", {UInt(16, 8), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "sqshrn", Int(16, 4), "saturating_shift_right_narrow", {Int(32, 4), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "uqshrn", UInt(16, 4), "saturating_shift_right_narrow", {UInt(32, 4), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "sqshrn", Int(32, 2), "saturating_shift_right_narrow", {Int(64, 2), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "uqshrn", UInt(32, 2), "saturating_shift_right_narrow", {UInt(64, 2), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "sqshrun", UInt(8, 8), "saturating_shift_right_narrow", {Int(16, 8), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "sqshrun", UInt(16, 4), "saturating_shift_right_narrow", {Int(32, 4), UInt(32)}, ArmIntrinsic::SveUnavailable},
    {nullptr, "sqshrun", UInt(32, 2), "saturating_shift_right_narrow", {Int(64, 2), UInt(32)}, ArmIntrinsic::SveUnavailable},

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
    // In SVE, no equivalent is found, though there are rounding, saturating, or widening versions.
    {"vshifts", "sshl", Int(8, 8), "shift_left", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {"vshiftu", "ushl", UInt(8, 8), "shift_left", {UInt(8, 8), Int(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {"vshifts", "sshl", Int(16, 4), "shift_left", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {"vshiftu", "ushl", UInt(16, 4), "shift_left", {UInt(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {"vshifts", "sshl", Int(32, 2), "shift_left", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {"vshiftu", "ushl", UInt(32, 2), "shift_left", {UInt(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {"vshifts", "sshl", Int(64, 2), "shift_left", {Int(64, 2), Int(64, 2)}, ArmIntrinsic::SveUnavailable},
    {"vshiftu", "ushl", UInt(64, 2), "shift_left", {UInt(64, 2), Int(64, 2)}, ArmIntrinsic::SveUnavailable},

    // SRSHR, URSHR - Rounding shift right (by immediate in [1, output bits])
    // LLVM wants these expressed as SRSHL by negative amounts.

    // SSHLL, USHLL - Shift left long (by immediate in [0, output bits - 1])
    // LLVM pattern matches these for us.

    // RADDHN - Add and narrow with rounding.
    {"vraddhn", "raddhn", Int(8, 8), "rounding_add_narrow", {Int(16, 8), Int(16, 8)}, ArmIntrinsic::SveUnavailable},
    {"vraddhn", "raddhn", UInt(8, 8), "rounding_add_narrow", {UInt(16, 8), UInt(16, 8)}, ArmIntrinsic::SveUnavailable},
    {"vraddhn", "raddhn", Int(16, 4), "rounding_add_narrow", {Int(32, 4), Int(32, 4)}, ArmIntrinsic::SveUnavailable},
    {"vraddhn", "raddhn", UInt(16, 4), "rounding_add_narrow", {UInt(32, 4), UInt(32, 4)}, ArmIntrinsic::SveUnavailable},
    {"vraddhn", "raddhn", Int(32, 2), "rounding_add_narrow", {Int(64, 2), Int(64, 2)}, ArmIntrinsic::SveUnavailable},
    {"vraddhn", "raddhn", UInt(32, 2), "rounding_add_narrow", {UInt(64, 2), UInt(64, 2)}, ArmIntrinsic::SveUnavailable},

    // RSUBHN - Sub and narrow with rounding.
    {"vrsubhn", "rsubhn", Int(8, 8), "rounding_sub_narrow", {Int(16, 8), Int(16, 8)}, ArmIntrinsic::SveUnavailable},
    {"vrsubhn", "rsubhn", UInt(8, 8), "rounding_sub_narrow", {UInt(16, 8), UInt(16, 8)}, ArmIntrinsic::SveUnavailable},
    {"vrsubhn", "rsubhn", Int(16, 4), "rounding_sub_narrow", {Int(32, 4), Int(32, 4)}, ArmIntrinsic::SveUnavailable},
    {"vrsubhn", "rsubhn", UInt(16, 4), "rounding_sub_narrow", {UInt(32, 4), UInt(32, 4)}, ArmIntrinsic::SveUnavailable},
    {"vrsubhn", "rsubhn", Int(32, 2), "rounding_sub_narrow", {Int(64, 2), Int(64, 2)}, ArmIntrinsic::SveUnavailable},
    {"vrsubhn", "rsubhn", UInt(32, 2), "rounding_sub_narrow", {UInt(64, 2), UInt(64, 2)}, ArmIntrinsic::SveUnavailable},

    // SQDMULH - Saturating doubling multiply keep high half.
    {"vqdmulh", "sqdmulh", Int(16, 4), "qdmulh", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveNoPredicate},
    {"vqdmulh", "sqdmulh", Int(32, 2), "qdmulh", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveNoPredicate},

    // SQRDMULH - Saturating doubling multiply keep high half with rounding.
    {"vqrdmulh", "sqrdmulh", Int(16, 4), "qrdmulh", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveNoPredicate},
    {"vqrdmulh", "sqrdmulh", Int(32, 2), "qrdmulh", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveNoPredicate},

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

    {nullptr, "addp", Int(8, 8), "pairwise_add", {Int(8, 16)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "addp", UInt(8, 8), "pairwise_add", {UInt(8, 16)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "addp", Int(16, 4), "pairwise_add", {Int(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "addp", UInt(16, 4), "pairwise_add", {UInt(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "addp", Int(32, 2), "pairwise_add", {Int(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "addp", UInt(32, 2), "pairwise_add", {UInt(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "addp", Int(64, 2), "pairwise_add", {Int(64, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::SveUnavailable},
    {nullptr, "addp", UInt(64, 2), "pairwise_add", {UInt(64, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::SveUnavailable},
    {nullptr, "faddp", Float(32, 2), "pairwise_add", {Float(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "faddp", Float(64, 2), "pairwise_add", {Float(64, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::SveUnavailable},
    {nullptr, "faddp", Float(16, 4), "pairwise_add", {Float(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16 | ArmIntrinsic::SveUnavailable},

    // SADDLP, UADDLP - Pairwise add long.
    {"vpaddls", "saddlp", Int(16, 4), "pairwise_widening_add", {Int(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveUnavailable},
    {"vpaddlu", "uaddlp", UInt(16, 4), "pairwise_widening_add", {UInt(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveUnavailable},
    {"vpaddlu", "uaddlp", Int(16, 4), "pairwise_widening_add", {UInt(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveUnavailable},
    {"vpaddls", "saddlp", Int(32, 2), "pairwise_widening_add", {Int(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveUnavailable},
    {"vpaddlu", "uaddlp", UInt(32, 2), "pairwise_widening_add", {UInt(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveUnavailable},
    {"vpaddlu", "uaddlp", Int(32, 2), "pairwise_widening_add", {UInt(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveUnavailable},
    {"vpaddls", "saddlp", Int(64, 1), "pairwise_widening_add", {Int(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::ScalarsAreVectors | ArmIntrinsic::SveUnavailable},
    {"vpaddlu", "uaddlp", UInt(64, 1), "pairwise_widening_add", {UInt(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::ScalarsAreVectors | ArmIntrinsic::SveUnavailable},
    {"vpaddlu", "uaddlp", Int(64, 1), "pairwise_widening_add", {UInt(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleRetArgs | ArmIntrinsic::ScalarsAreVectors | ArmIntrinsic::SveUnavailable},

    // // SPADAL, UPADAL - Pairwise add and accumulate long.
    {"vpadals", "sadalp", Int(16, 4), "pairwise_widening_add_accumulate", {Int(16, 4), Int(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs | ArmIntrinsic::Neon64Unavailable},
    {"vpadalu", "uadalp", UInt(16, 4), "pairwise_widening_add_accumulate", {UInt(16, 4), UInt(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs | ArmIntrinsic::Neon64Unavailable},
    {"vpadalu", "uadalp", Int(16, 4), "pairwise_widening_add_accumulate", {Int(16, 4), UInt(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs | ArmIntrinsic::Neon64Unavailable},
    {"vpadals", "sadalp", Int(32, 2), "pairwise_widening_add_accumulate", {Int(32, 2), Int(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs | ArmIntrinsic::Neon64Unavailable},
    {"vpadalu", "uadalp", UInt(32, 2), "pairwise_widening_add_accumulate", {UInt(32, 2), UInt(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs | ArmIntrinsic::Neon64Unavailable},
    {"vpadalu", "uadalp", Int(32, 2), "pairwise_widening_add_accumulate", {Int(32, 2), UInt(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs | ArmIntrinsic::Neon64Unavailable},
    {"vpadals", "sadalp", Int(64, 1), "pairwise_widening_add_accumulate", {Int(64, 1), Int(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs | ArmIntrinsic::ScalarsAreVectors | ArmIntrinsic::Neon64Unavailable},
    {"vpadalu", "uadalp", UInt(64, 1), "pairwise_widening_add_accumulate", {UInt(64, 1), UInt(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs | ArmIntrinsic::ScalarsAreVectors | ArmIntrinsic::Neon64Unavailable},
    {"vpadalu", "uadalp", Int(64, 1), "pairwise_widening_add_accumulate", {Int(64, 1), UInt(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::MangleArgs | ArmIntrinsic::ScalarsAreVectors | ArmIntrinsic::Neon64Unavailable},

    // SMAXP, UMAXP, FMAXP - Pairwise max.
    {nullptr, "smaxp", Int(8, 8), "pairwise_max", {Int(8, 16)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "umaxp", UInt(8, 8), "pairwise_max", {UInt(8, 16)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "smaxp", Int(16, 4), "pairwise_max", {Int(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "umaxp", UInt(16, 4), "pairwise_max", {UInt(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "smaxp", Int(32, 2), "pairwise_max", {Int(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "umaxp", UInt(32, 2), "pairwise_max", {UInt(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "fmaxp", Float(32, 2), "pairwise_max", {Float(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "fmaxp", Float(16, 4), "pairwise_max", {Float(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16 | ArmIntrinsic::SveUnavailable},

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
    {nullptr, "sminp", Int(8, 8), "pairwise_min", {Int(8, 16)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "uminp", UInt(8, 8), "pairwise_min", {UInt(8, 16)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "sminp", Int(16, 4), "pairwise_min", {Int(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "uminp", UInt(16, 4), "pairwise_min", {UInt(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "sminp", Int(32, 2), "pairwise_min", {Int(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "uminp", UInt(32, 2), "pairwise_min", {UInt(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "fminp", Float(32, 2), "pairwise_min", {Float(32, 4)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::SveUnavailable},
    {nullptr, "fminp", Float(16, 4), "pairwise_min", {Float(16, 8)}, ArmIntrinsic::SplitArg0 | ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16 | ArmIntrinsic::SveUnavailable},

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
    {nullptr, "sdot", Int(32, 2), "dot_product", {Int(32, 2), Int(8, 8), Int(8, 8)}, ArmIntrinsic::MangleRetArg1 | ArmIntrinsic::SveUnavailable},
    {nullptr, "udot", Int(32, 2), "dot_product", {Int(32, 2), UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::MangleRetArg1 | ArmIntrinsic::SveUnavailable},
    {nullptr, "udot", UInt(32, 2), "dot_product", {UInt(32, 2), UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::MangleRetArg1 | ArmIntrinsic::SveUnavailable},
    {nullptr, "sdot", Int(32, 4), "dot_product", {Int(32, 4), Int(8, 16), Int(8, 16)}, ArmIntrinsic::MangleRetArg1 | ArmIntrinsic::SveNoPredicate},
    {nullptr, "udot", Int(32, 4), "dot_product", {Int(32, 4), UInt(8, 16), UInt(8, 16)}, ArmIntrinsic::MangleRetArg1 | ArmIntrinsic::SveNoPredicate},
    {nullptr, "udot", UInt(32, 4), "dot_product", {UInt(32, 4), UInt(8, 16), UInt(8, 16)}, ArmIntrinsic::MangleRetArg1 | ArmIntrinsic::SveNoPredicate},
    {nullptr, "sdot", Int(64, 2), "dot_product", {Int(64, 2), Int(16, 8), Int(16, 8)}, ArmIntrinsic::MangleRetArg1 | ArmIntrinsic::SveNoPredicate | ArmIntrinsic::Neon64Unavailable},
    {nullptr, "udot", Int(64, 2), "dot_product", {Int(64, 2), UInt(16, 8), UInt(16, 8)}, ArmIntrinsic::MangleRetArg1 | ArmIntrinsic::SveNoPredicate | ArmIntrinsic::Neon64Unavailable},
    {nullptr, "udot", UInt(64, 2), "dot_product", {UInt(64, 2), UInt(16, 8), UInt(16, 8)}, ArmIntrinsic::MangleRetArg1 | ArmIntrinsic::SveNoPredicate | ArmIntrinsic::Neon64Unavailable},

    // ABDL - Widening absolute difference
    // The ARM backend folds both signed and unsigned widening casts of absd to a widening_absd, so we need to handle both signed and
    // unsigned input and return types.
    {"vabdl_i8x8", "vabdl_i8x8", Int(16, 8), "widening_absd", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix | ArmIntrinsic::SveUnavailable},
    {"vabdl_i8x8", "vabdl_i8x8", UInt(16, 8), "widening_absd", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix | ArmIntrinsic::SveUnavailable},
    {"vabdl_u8x8", "vabdl_u8x8", Int(16, 8), "widening_absd", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix | ArmIntrinsic::SveUnavailable},
    {"vabdl_u8x8", "vabdl_u8x8", UInt(16, 8), "widening_absd", {UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix | ArmIntrinsic::SveUnavailable},
    {"vabdl_i16x4", "vabdl_i16x4", Int(32, 4), "widening_absd", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix | ArmIntrinsic::SveUnavailable},
    {"vabdl_i16x4", "vabdl_i16x4", UInt(32, 4), "widening_absd", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix | ArmIntrinsic::SveUnavailable},
    {"vabdl_u16x4", "vabdl_u16x4", Int(32, 4), "widening_absd", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix | ArmIntrinsic::SveUnavailable},
    {"vabdl_u16x4", "vabdl_u16x4", UInt(32, 4), "widening_absd", {UInt(16, 4), UInt(16, 4)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix | ArmIntrinsic::SveUnavailable},
    {"vabdl_i32x2", "vabdl_i32x2", Int(64, 2), "widening_absd", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix | ArmIntrinsic::SveUnavailable},
    {"vabdl_i32x2", "vabdl_i32x2", UInt(64, 2), "widening_absd", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix | ArmIntrinsic::SveUnavailable},
    {"vabdl_u32x2", "vabdl_u32x2", Int(64, 2), "widening_absd", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix | ArmIntrinsic::SveUnavailable},
    {"vabdl_u32x2", "vabdl_u32x2", UInt(64, 2), "widening_absd", {UInt(32, 2), UInt(32, 2)}, ArmIntrinsic::NoMangle | ArmIntrinsic::NoPrefix | ArmIntrinsic::SveUnavailable},
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
    "round_f16",
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

llvm::Function *CodeGen_ARM::define_intrin_wrapper(const std::string &inner_name,
                                                   const Type &ret_type,
                                                   const std::string &mangled_name,
                                                   const std::vector<Type> &arg_types,
                                                   bool add_inactive_arg,
                                                   bool add_predicate,
                                                   bool split_arg0,
                                                   bool scalars_are_vectors) {

    if (!(add_inactive_arg || add_predicate || split_arg0)) {
        // No need to wrap
        return get_llvm_intrin(ret_type, mangled_name, arg_types, scalars_are_vectors);
    }

    std::vector<Type> inner_arg_types;
    std::vector<Value *> inner_args;
    internal_assert(!arg_types.empty());
    const int inner_lanes = split_arg0 ? arg_types[0].lanes() / 2 : arg_types[0].lanes();

    if (add_inactive_arg) {
        // The fallback value has the same type as ret value.
        // We don't use this, so just pad it with 0.
        inner_arg_types.push_back(ret_type);

        Value *zero = Constant::getNullValue(llvm_type_of(ret_type));
        inner_args.push_back(zero);
    }
    if (add_predicate) {
        Type pred_type = Int(1, inner_lanes);
        inner_arg_types.push_back(pred_type);
        // For now, we don't use predicate in overloaded intrinsic
        Value *ptrue = Constant::getAllOnesValue(llvm_type_of(pred_type));
        inner_args.push_back(ptrue);
    }
    if (split_arg0) {
        Type split_arg_type = arg_types[0].with_lanes(inner_lanes);
        inner_arg_types.push_back(split_arg_type);
        inner_arg_types.push_back(split_arg_type);
        internal_assert(arg_types.size() == 1);
    } else {
        // Push back all argument typs which Halide defines
        std::copy(arg_types.begin(), arg_types.end(), std::back_inserter(inner_arg_types));
    }

    llvm::Function *inner = get_llvm_intrin(ret_type, mangled_name, inner_arg_types, scalars_are_vectors);
    llvm::FunctionType *inner_ty = inner->getFunctionType();

    auto to_llvm_type = [&](const Type &t) {
        llvm::Type *ret = llvm_type_of(t);
        if (t.is_scalar() && scalars_are_vectors) {
            ret = get_vector_type(ret, 1);
        }
        return ret;
    };
    std::vector<llvm::Type *> llvm_arg_types;
    std::transform(arg_types.begin(), arg_types.end(), std::back_inserter(llvm_arg_types), to_llvm_type);
    llvm::FunctionType *wrapper_ty = llvm::FunctionType::get(inner_ty->getReturnType(), llvm_arg_types, false);

    string wrapper_name = inner_name + unique_name("_wrapper");
    llvm::Function *wrapper =
        llvm::Function::Create(wrapper_ty, llvm::GlobalValue::InternalLinkage, wrapper_name, module.get());
    llvm::BasicBlock *block =
        llvm::BasicBlock::Create(module->getContext(), "entry", wrapper);
    IRBuilderBase::InsertPoint here = builder->saveIP();
    builder->SetInsertPoint(block);

    if (split_arg0) {
        // Call the real intrinsic.
        Value *low = slice_vector(wrapper->getArg(0), 0, inner_lanes);
        Value *high = slice_vector(wrapper->getArg(0), inner_lanes, inner_lanes);
        inner_args.push_back(low);
        inner_args.push_back(high);
        internal_assert(inner_arg_types.size() == 2);
    } else {
        for (auto *itr = wrapper->arg_begin(); itr != wrapper->arg_end(); ++itr) {
            inner_args.push_back(itr);
        }
    }

    // Call the real intrinsic.
    Value *ret = builder->CreateCall(inner, inner_args);
    builder->CreateRet(ret);

    // Always inline these wrappers.
    wrapper->addFnAttr(llvm::Attribute::AlwaysInline);

    builder->restoreIP(here);

    llvm::verifyFunction(*wrapper);
    return wrapper;
}

int CodeGen_ARM::target_vscale() const {
    if (target.has_feature(Target::SVE)) {
        user_error << "Feature SVE is not supported yet, please set SVE2 if the target has the capability\n";
        return 0;
    }

    if (target.features_any_of({Target::SVE, Target::SVE2})) {
        if (target.vector_bits == 0) {
            user_error << "Please set vector_bits in Target with SVE/SVE2 feature. Halide asserts target vector length in compilation time\n";
            return 0;
        } else if (target.vector_bits % 128 != 0) {
            user_error << "Unexpected vector_bits " << target.vector_bits << ", which is not multiple of 128\n";
            return 0;
        } else {
            int vscale = target.vector_bits / 128;
            if (vscale != 1) {
                user_error << "vector_bits other than 128 bits is not supported yet\n";
                return 0;
            }
            return vscale;
        }
    } else {  // without SVE/SVE2
        if (target.vector_bits != 0) {
            user_warning << "vector_bits is set in the target which doesn't have scalable vector feature\n";
        }
        return 0;
    }
    return 0;
}

void CodeGen_ARM::init_module() {
    CodeGen_Posix::init_module();

    if (neon_intrinsics_disabled()) {
        return;
    }

    const bool is_sve = target.has_feature(Target::SVE2);
    string prefix = target.bits == 32 ? "llvm.arm.neon." : is_sve ? "llvm.aarch64.sve." :
                                                                    "llvm.aarch64.neon.";
    for (const ArmIntrinsic &intrin : intrinsic_defs) {
        if (intrin.flags & ArmIntrinsic::RequireFp16 && !target.has_feature(Target::ARMFp16)) {
            continue;
        }
        if (intrin.flags & ArmIntrinsic::SveUnavailable && is_sve) {
            continue;
        }
        if (intrin.flags & ArmIntrinsic::Neon64Unavailable && (target.bits == 64 && !is_sve)) {
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
        const bool is_vanilla_intrinsic = starts_with(full_name, "llvm.");
        if (!is_vanilla_intrinsic && (intrin.flags & ArmIntrinsic::NoPrefix) == 0) {
            full_name = prefix + full_name;
        }

        // We might have to generate versions of this intrinsic with multiple widths.
        vector<int> width_factors = {1};
        if (intrin.flags & ArmIntrinsic::HalfWidth) {
            if (is_sve) {
                // In case of SVE, full version only
                width_factors[0] = 2;
            } else {
                width_factors.push_back(2);
            }
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
                arg_type = arg_type.with_lanes(arg_type.lanes() * width_factor);
                arg_types.emplace_back(arg_type);
            }

            // Generate the LLVM mangled name.
            std::stringstream mangled_name_builder;
            mangled_name_builder << full_name;
            if (starts_with(full_name, "llvm.") && (intrin.flags & ArmIntrinsic::NoMangle) == 0) {
                // Append LLVM name mangling for either the return type or the arguments, or both.
                vector<Type> types;
                if (intrin.flags & ArmIntrinsic::MangleArgs && !is_sve) {
                    types = arg_types;
                } else if (intrin.flags & ArmIntrinsic::MangleRetArgs) {
                    types = {ret_type};
                    types.insert(types.end(), arg_types.begin(), arg_types.end());
                } else if (intrin.flags & ArmIntrinsic::MangleRetArg1 && !is_sve) {
                    internal_assert(arg_types.size() >= 2);
                    types = {ret_type, arg_types[1]};
                } else {
                    types = {ret_type};
                }
                for (const Type &t : types) {
                    mangled_name_builder << (is_sve ? ".nxv" : ".v") << t.lanes();
                    if (t.is_int() || t.is_uint()) {
                        mangled_name_builder << "i";
                    } else if (t.is_float()) {
                        mangled_name_builder << "f";
                    }
                    mangled_name_builder << t.bits();
                }
            }
            string mangled_name = mangled_name_builder.str();

            const bool require_predicate = is_sve && !is_vanilla_intrinsic && !(intrin.flags & ArmIntrinsic::SveNoPredicate);

            llvm::Function *intrin_impl = define_intrin_wrapper(
                intrin.name,
                ret_type, mangled_name, arg_types,
                is_sve && intrin.flags & ArmIntrinsic::SveInactiveArg,
                require_predicate,
                intrin.flags & ArmIntrinsic::SplitArg0,
                intrin.flags & ArmIntrinsic::ScalarsAreVectors);
#if LLVM_VERSION >= 140
            if (debug::debug_level() >= 4) {
                intrin_impl->dump();
            }
#endif
            intrin_impl->addFnAttr(llvm::Attribute::ReadNone);
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

Expr runtime_vscale() {
    return Call::make(Int(32), "get_runtime_vscale", std::vector<Expr>(), Call::PureExtern);
}

void CodeGen_ARM::begin_func(LinkageType linkage, const std::string &simple_name,
                             const std::string &extern_name, const std::vector<LoweredArgument> &args) {
    CodeGen_Posix::begin_func(linkage, simple_name, extern_name, args);

    if (effective_vscale != 0 && !target.has_feature(Target::NoAsserts)) {
        // Make sure run-time vscale is equal to compile-time vscale
        Value *val_runtime_vscale = codegen(runtime_vscale());
        Value *val_compiletime_vscale = ConstantInt::get(i32_t, effective_vscale);
        Value *cond = builder->CreateICmpEQ(val_runtime_vscale, val_compiletime_vscale);
        create_assertion(cond, Call::make(Int(32), "halide_error_vscale_invalid",
                                          {simple_name, runtime_vscale(), Expr(effective_vscale)}, Call::Extern));
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
                if (Value *v = call_overloaded_intrin(op->type, pattern.intrin, matches)) {
                    value = v;
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
                if (Value *v = call_overloaded_intrin(t, "widening_absd", absd->args)) {
                    value = v;
                    return;
                }
            }
        }

        // If we didn't find a pattern, try rewriting the cast.
        static const vector<pair<Expr, Expr>> cast_rewrites = {
            // Double or triple narrowing saturating casts are better expressed as
            // regular narrowing casts.
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

    // LLVM fptoui generates fcvtzs or fcvtzu in inconsistent way
    if (op->value.type().is_float() && op->type.is_int_or_uint()) {
        if (Value *v = call_overloaded_intrin(op->type, "fp_to_int", {op->value})) {
            value = v;
            return;
        }
    }

    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    if (op->value.type().is_float() && op->type.is_float()) {
        codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
            internal_assert(args.size() == 1);
            return builder->CreateFPCast(args[0], llvm_type_of(op->type.with_lanes(lanes)));
        };
        if (codegen_with_natural_lanes_if_necessary(op->type, {op->value}, cg_func)) {
            return;
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
                if (Value *v = call_overloaded_intrin(op->type, i.intrin, matches)) {
                    value = v;
                    return;
                }
            }
        }
    }

    // Peep-hole (0 - b) pattern to generate "negate" instruction
    if (is_const_zero(op->a)) {
        if (effective_vscale != 0) {
            if ((op->type.bits() >= 8 && op->type.is_int()) ||
                (op->type.bits() >= 16 && op->type.is_float())) {
                if (Value *v = call_overloaded_intrin(op->type, "negate", {op->b})) {
                    value = v;
                    return;
                }
            }

        } else {
            // llvm.neon.neg/fneg intrinsic doesn't seem to exist. Instead,
            // llvm will generate floating point negate instructions if we ask for (-0.0f)-x
            if (op->type.is_float() &&
                (op->type.bits() >= 32 || is_float16_and_has_feature(op->type))) {
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
                    a = ConstantVector::getSplat(element_count(op->type.lanes()), a);
                }
                value = builder->CreateFSub(a, b);
                return;
            }
        }
    }

    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    if (op->type.is_float() || op->type.bits() == 64) {
        codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
            internal_assert(args.size() == 2);
            if (op->type.is_float()) {
                return builder->CreateFSub(args[0], args[1]);
            } else if (op->type.is_int() && op->type.bits() >= 32) {
                return builder->CreateNSWSub(args[0], args[1]);
            } else {
                return builder->CreateSub(args[0], args[1]);
            }
        };
        if (codegen_with_natural_lanes_if_necessary(op->type, {op->a, op->b}, cg_func)) {
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Add *op) {
    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    if (op->type.is_float() || op->type.bits() == 64) {
        codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
            internal_assert(args.size() == 2);
            if (op->type.is_float()) {
                return builder->CreateFAdd(args[0], args[1]);
            } else if (op->type.is_int() && op->type.bits() >= 32) {
                return builder->CreateNSWAdd(args[0], args[1]);
            } else {
                return builder->CreateAdd(args[0], args[1]);
            }
        };
        if (codegen_with_natural_lanes_if_necessary(op->type, {op->a, op->b}, cg_func)) {
            return;
        }
    }
    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Mul *op) {
    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    if (op->type.is_float() || op->type.bits() == 64) {
        codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
            internal_assert(args.size() == 2);
            if (op->type.is_float()) {
                return builder->CreateFMul(args[0], args[1]);
            } else if (op->type.is_int() && op->type.bits() >= 32) {
                return builder->CreateNSWMul(args[0], args[1]);
            } else {
                return builder->CreateMul(args[0], args[1]);
            }
        };
        if (codegen_with_natural_lanes_if_necessary(op->type, {op->a, op->b}, cg_func)) {
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Div *op) {
    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    if (op->type.is_float()) {
        codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
            internal_assert(args.size() == 2);
            return builder->CreateFDiv(args[0], args[1]);
        };
        if (codegen_with_natural_lanes_if_necessary(op->type, {op->a, op->b}, cg_func)) {
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Min *op) {
    // Use a 2-wide vector for scalar floats.
    if (!neon_intrinsics_disabled() && (op->type.is_float() || op->type.is_vector())) {
        if (Value *v = call_overloaded_intrin(op->type, "min", {op->a, op->b})) {
            value = v;
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Max *op) {
    // Use a 2-wide vector for scalar floats.
    if (!neon_intrinsics_disabled() && (op->type.is_float() || op->type.is_vector())) {
        if (Value *v = call_overloaded_intrin(op->type, "max", {op->a, op->b})) {
            value = v;
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Store *op) {
    // Predicated store
    const bool is_predicated_store = !is_const_one(op->predicate);
    if (is_predicated_store && !target.has_feature(Target::SVE2)) {
        CodeGen_Posix::visit(op);
        return;
    }

    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    // A dense store of an interleaving can be done using a vst2 intrinsic
    const Ramp *ramp = op->index.as<Ramp>();

    // We only deal with ramps here except for SVE2
    if (!ramp && !target.has_feature(Target::SVE2)) {
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
        if (elt == Float(32) || elt == Float(64) ||
            is_float16_and_has_feature(elt) ||
            elt == Int(8) || elt == Int(16) || elt == Int(32) || elt == Int(64) ||
            elt == UInt(8) || elt == UInt(16) || elt == UInt(32) || elt == UInt(64)) {
            if (vec_bits % 64 == 0) {
                type_ok_for_vst = true;
                auto intrin_bits = (vec_bits % 128 == 0 || target.has_feature(Target::SVE2)) ? 128 : 64;
                intrin_type = intrin_type.with_lanes(intrin_bits / t.bits());
            }
        }
    }

    if (ramp && is_const_one(ramp->stride) &&
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
        Value *store_pred_val = codegen(op->predicate);

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
            if (target.has_feature(Target::SVE2)) {
                instr << "llvm.aarch64.sve.st"
                      << num_vecs
                      << ".nxv"
                      << intrin_type.lanes()
                      << (t.is_float() ? 'f' : 'i')
                      << t.bits();
                arg_types = vector<llvm::Type *>(num_vecs, llvm_type_of(intrin_type));
                arg_types.emplace_back(get_vector_type(i1_t, intrin_type.lanes()));  // predicate
                arg_types.emplace_back(llvm_type_of(intrin_type.element_of())->getPointerTo());
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
        }
        llvm::FunctionType *fn_type = FunctionType::get(llvm::Type::getVoidTy(*context), arg_types, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(instr.str(), fn_type);
        internal_assert(fn);

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
                if (target.has_feature(Target::SVE2)) {
                    // Set the predicate argument
                    auto active_lanes = std::min(t.lanes() - i, intrin_type.lanes());
                    Value *vpred_val;
                    if (is_predicated_store) {
                        vpred_val = slice_vector(store_pred_val, i, intrin_type.lanes());
                    } else {
                        Expr vpred = make_vector_predicate_1s_0s(active_lanes, intrin_type.lanes() - active_lanes);
                        vpred_val = codegen(vpred);
                    }
                    slice_args.push_back(vpred_val);
                }
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

    if (target.has_feature(Target::SVE2)) {
        const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : nullptr;
        if (stride && stride->value == 1) {
            // Basically we can deal with vanilla codegen,
            // but to avoid LLVM error, process with the multiple of natural_lanes
            const int natural_lanes = target.natural_vector_size(op->value.type());
            if (ramp->lanes % natural_lanes) {
                int aligned_lanes = align_up(ramp->lanes, natural_lanes);
                // Use predicate to prevent overrun
                Expr vpred;
                if (is_predicated_store) {
                    vpred = Shuffle::make_concat({op->predicate, const_false(aligned_lanes - ramp->lanes)});
                } else {
                    vpred = make_vector_predicate_1s_0s(ramp->lanes, aligned_lanes - ramp->lanes);
                }
                auto aligned_index = Ramp::make(ramp->base, stride, aligned_lanes);
                Expr padding = make_zero(op->value.type().with_lanes(aligned_lanes - ramp->lanes));
                Expr aligned_value = Shuffle::make_concat({op->value, padding});
                codegen(Store::make(op->name, aligned_value, aligned_index, op->param, vpred, op->alignment));
                return;
            }
        } else if (op->index.type().is_vector()) {
            // Scatter
            Type elt = op->value.type().element_of();

            // Rewrite float16 case into reinterpret and Store in uint16, as it is unsupported in LLVM
            if (is_float16_and_has_feature(elt)) {
                Type u16_type = op->value.type().with_code(halide_type_uint);
                Expr v = reinterpret(u16_type, op->value);
                codegen(Store::make(op->name, v, op->index, op->param, op->predicate, op->alignment));
                return;
            }

            const int store_lanes = op->value.type().lanes();
            const int index_bits = 32;
            Type type_with_max_bits = Int(std::max(elt.bits(), index_bits));
            // The number of lanes is constrained by index vector type
            const int natural_lanes = target.natural_vector_size(type_with_max_bits);

            Expr base = 0;
            Value *elt_ptr = codegen_buffer_pointer(op->name, elt, base);
            Value *val = codegen(op->value);
            Value *index = codegen(op->index);
            Value *store_pred_val = codegen(op->predicate);

            llvm::Type *slice_type = get_vector_type(llvm_type_of(elt), natural_lanes);
            llvm::Type *slice_index_type = get_vector_type(llvm_type_of(op->index.type().element_of()), natural_lanes);
            llvm::Type *pred_type = get_vector_type(llvm_type_of(op->predicate.type().element_of()), natural_lanes);

            std::ostringstream instr;
            instr << "llvm.aarch64.sve.st1.scatter.uxtw."
                  << (elt.bits() != 8 ? "index." : "")  // index is scaled into bytes
                  << "nxv"
                  << natural_lanes
                  << (elt == Float(32) || elt == Float(64) ? 'f' : 'i')
                  << elt.bits();

            vector<llvm::Type *> arg_types{slice_type, pred_type, elt_ptr->getType(), slice_index_type};
            llvm::FunctionType *fn_type = FunctionType::get(void_t, arg_types, false);
            FunctionCallee fn = module->getOrInsertFunction(instr.str(), fn_type);

            // We need to slice the result into native vector lanes to use intrinsic
            for (int i = 0; i < store_lanes; i += natural_lanes) {
                Value *slice_value = slice_vector(val, i, natural_lanes);
                Value *slice_index = slice_vector(index, i, natural_lanes);
                const int active_lanes = std::min(store_lanes - i, natural_lanes);

                Expr vpred = make_vector_predicate_1s_0s(active_lanes, natural_lanes - active_lanes);
                Value *vpred_val = codegen(vpred);
                if (is_predicated_store) {
                    Value *sliced_store_vpred_val = slice_vector(store_pred_val, i, natural_lanes);
                    vpred_val = builder->CreateAnd(vpred_val, sliced_store_vpred_val);
                }

                CallInst *store = builder->CreateCall(fn, {slice_value, vpred_val, elt_ptr, slice_index});
                add_tbaa_metadata(store, op->name, op->index);
            }

            return;
        }
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
    const bool is_predicated_load = !is_const_one(op->predicate);
    if (is_predicated_load && !target.has_feature(Target::SVE2)) {
        CodeGen_Posix::visit(op);
        return;
    }

    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    const Ramp *ramp = op->index.as<Ramp>();

    // We only deal with ramps except for SVE
    if (!ramp && !target.has_feature(Target::SVE2)) {
        CodeGen_Posix::visit(op);
        return;
    }

    const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : nullptr;
    if (stride && (-1 <= stride->value && stride->value <= 4) && !target.has_feature(Target::SVE2)) {
        // If the stride is in [-1, 4], we can deal with that using vanilla codegen
        // For SVE, we still need to deal with here
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

    if (target.has_feature(Target::SVE2)) {
        if (stride && stride->value < 1) {
            CodeGen_Posix::visit(op);
            return;
        } else if (stride && stride->value == 1) {
            const int natural_lanes = target.natural_vector_size(op->type);
            if (ramp->lanes % natural_lanes) {
                // Load with lanes multiple of natural_lanes
                int aligned_lanes = align_up(ramp->lanes, natural_lanes);
                // Use predicate to prevent from overrun
                Expr vpred;
                if (is_predicated_load) {
                    vpred = Shuffle::make_concat({op->predicate, const_false(aligned_lanes - ramp->lanes)});
                } else {
                    vpred = make_vector_predicate_1s_0s(ramp->lanes, aligned_lanes - ramp->lanes);
                }
                auto aligned_index = Ramp::make(ramp->base, stride, aligned_lanes);
                auto aligned_type = op->type.with_lanes(aligned_lanes);
                value = codegen(Load::make(aligned_type, op->name, aligned_index, op->image, op->param, vpred, op->alignment));
                value = slice_vector(value, 0, ramp->lanes);
                return;
            } else {
                CodeGen_Posix::visit(op);
                return;
            }
        } else if (stride && (2 <= stride->value && stride->value <= 4)) {
            // Structured load ST2/ST3/ST4 of SVE

            Expr base = ramp->base;
            ModulusRemainder align = op->alignment;

            int aligned_stride = gcd(stride->value, align.modulus);
            int offset = 0;
            if (aligned_stride == stride->value) {
                offset = mod_imp((int)align.remainder, aligned_stride);
            } else {
                const Add *add = base.as<Add>();
                if (const IntImm *add_c = add ? add->b.as<IntImm>() : base.as<IntImm>()) {
                    offset = mod_imp(add_c->value, stride->value);
                }
            }

            if (offset) {
                base = simplify(base - offset);
            }

            Value *load_pred_val = codegen(op->predicate);

            // We need to slice the result in to native vector lanes to use sve intrin.
            // LLVM will optimize redundant ld instructions afterwards
            const int slice_lanes = target.natural_vector_size(op->type);
            vector<Value *> results;
            for (int i = 0; i < op->type.lanes(); i += slice_lanes) {
                int load_base_i = i * stride->value;
                Expr slice_base = simplify(base + load_base_i);
                Expr slice_index = Ramp::make(slice_base, stride, slice_lanes);
                std::ostringstream instr;
                instr << "llvm.aarch64.sve.ld"
                      << stride->value
                      << ".sret.nxv"
                      << slice_lanes
                      << (op->type.is_float() ? 'f' : 'i')
                      << op->type.bits();
                llvm::Type *elt = llvm_type_of(op->type.element_of());
                llvm::Type *slice_type = get_vector_type(elt, slice_lanes);
                StructType *sret_type = StructType::get(module->getContext(), std::vector(stride->value, slice_type));
                std::vector<llvm::Type *> arg_types{get_vector_type(i1_t, slice_lanes), PointerType::get(elt, 0)};
                llvm::FunctionType *fn_type = FunctionType::get(sret_type, arg_types, false);
                FunctionCallee fn = module->getOrInsertFunction(instr.str(), fn_type);

                // Set the predicate argument
                int active_lanes = std::min(op->type.lanes() - i, slice_lanes);

                Expr vpred = make_vector_predicate_1s_0s(active_lanes, slice_lanes - active_lanes);
                Value *vpred_val = codegen(vpred);
                if (is_predicated_load) {
                    Value *sliced_load_vpred_val = slice_vector(load_pred_val, i, slice_lanes);
                    vpred_val = builder->CreateAnd(vpred_val, sliced_load_vpred_val);
                }

                Value *elt_ptr = codegen_buffer_pointer(op->name, op->type.element_of(), slice_base);
                CallInst *load_i = builder->CreateCall(fn, {vpred_val, elt_ptr});
                add_tbaa_metadata(load_i, op->name, slice_index);
                // extract one element out of returned struct
                Value *extracted = builder->CreateExtractValue(load_i, offset);
                results.push_back(extracted);
            }

            // Retrieve original lanes
            value = concat_vectors(results);
            value = slice_vector(value, 0, op->type.lanes());
            return;
        } else if (op->index.type().is_vector()) {
            // General Gather Load

            // Rewrite float16 case into load in uint16 and reinterpret, as it is unsupported in LLVM
            if (is_float16_and_has_feature(op->type)) {
                Type u16_type = op->type.with_code(halide_type_uint);
                Expr equiv = Load::make(u16_type, op->name, op->index, op->image, op->param, op->predicate, op->alignment);
                equiv = reinterpret(op->type, equiv);
                equiv = common_subexpression_elimination(equiv);
                value = codegen(equiv);
                return;
            }

            Type elt = op->type.element_of();
            const int load_lanes = op->type.lanes();
            const int index_bits = 32;
            Type type_with_max_bits = Int(std::max(elt.bits(), index_bits));
            // The number of lanes is constrained by index vector type
            const int natural_lanes = target.natural_vector_size(type_with_max_bits);

            Expr base = 0;
            Value *elt_ptr = codegen_buffer_pointer(op->name, elt, base);
            Value *index = codegen(op->index);
            Value *load_pred_val = codegen(op->predicate);

            llvm::Type *slice_type = get_vector_type(llvm_type_of(elt), natural_lanes);
            llvm::Type *slice_index_type = get_vector_type(llvm_type_of(op->index.type().element_of()), natural_lanes);
            llvm::Type *pred_type = get_vector_type(llvm_type_of(op->predicate.type().element_of()), natural_lanes);

            std::ostringstream instr;
            instr << "llvm.aarch64.sve.ld1.gather.uxtw."
                  << (elt.bits() != 8 ? "index." : "")  // index is scaled into bytes
                  << "nxv"
                  << natural_lanes
                  << (elt == Float(32) || elt == Float(64) ? 'f' : 'i')
                  << elt.bits();

            llvm::FunctionType *fn_type = FunctionType::get(slice_type, {pred_type, elt_ptr->getType(), slice_index_type}, false);
            FunctionCallee fn = module->getOrInsertFunction(instr.str(), fn_type);

            // We need to slice the result in to native vector lanes to use intrinsic
            vector<Value *> results;
            for (int i = 0; i < load_lanes; i += natural_lanes) {
                Value *slice_index = slice_vector(index, i, natural_lanes);

                const int active_lanes = std::min(load_lanes - i, natural_lanes);

                Expr vpred = make_vector_predicate_1s_0s(active_lanes, natural_lanes - active_lanes);
                Value *vpred_val = codegen(vpred);
                if (is_predicated_load) {
                    Value *sliced_load_vpred_val = slice_vector(load_pred_val, i, natural_lanes);
                    vpred_val = builder->CreateAnd(vpred_val, sliced_load_vpred_val);
                }

                CallInst *gather = builder->CreateCall(fn, {vpred_val, elt_ptr, slice_index});
                add_tbaa_metadata(gather, op->name, op->index);
                results.push_back(gather);
            }

            // Retrieve original lanes
            value = concat_vectors(results);
            value = slice_vector(value, 0, load_lanes);
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Ramp *op) {
    if (effective_vscale != 0 && op->type.is_int_or_uint()) {
        if (is_const_zero(op->base) && is_const_one(op->stride)) {

            codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
                internal_assert(args.empty());
                // Generate stepvector intrinsic for ScalableVector
                return builder->CreateStepVector(llvm_type_of(op->type.with_lanes(lanes)));
            };
            // codgen with next-power-of-two lanes, because if we sliced into natural_lanes(e.g. 4),
            // it would produce {0,1,2,3,0,1,..} instead of {0,1,2,3,4,5,..}
            const int ret_lanes = op->type.lanes();
            const int aligned_lanes = next_power_of_two(ret_lanes);
            value = codegen_with_lanes(aligned_lanes, ret_lanes, {}, cg_func);
            return;
        } else {
            Expr broadcast_base = Broadcast::make(op->base, op->lanes);
            Expr broadcast_stride = Broadcast::make(op->stride, op->lanes);
            Expr step_ramp = Ramp::make(make_zero(op->base.type()), make_one(op->base.type()), op->lanes);
            value = codegen(broadcast_base + broadcast_stride * step_ramp);
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Call *op) {
    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

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
    }

    if (op->type.is_vector()) {
        vector<Expr> matches;
        for (const Pattern &pattern : calls) {
            if (expr_match(pattern.pattern, op, matches)) {
                if (Value *v = call_overloaded_intrin(op->type, pattern.intrin, matches)) {
                    value = v;
                    return;
                }
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

    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    if (effective_vscale != 0) {

        if (op->is_intrinsic(Call::bitwise_and)) {
            codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
                internal_assert(args.size() == 2);
                return builder->CreateAnd(args[0], args[1]);
            };
            if (codegen_with_natural_lanes_if_necessary(op->type, op->args, cg_func)) {
                return;
            }
        } else if (op->is_intrinsic(Call::bitwise_xor)) {
            codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
                internal_assert(args.size() == 2);
                return builder->CreateXor(args[0], args[1]);
            };
            if (codegen_with_natural_lanes_if_necessary(op->type, op->args, cg_func)) {
                return;
            }
        } else if (op->is_intrinsic(Call::bitwise_or)) {
            codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
                internal_assert(args.size() == 2);
                return builder->CreateOr(args[0], args[1]);
            };
            if (codegen_with_natural_lanes_if_necessary(op->type, op->args, cg_func)) {
                return;
            }
        } else if (op->is_intrinsic(Call::bitwise_not)) {
            codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
                internal_assert(args.size() == 1);
                return builder->CreateNot(args[0]);
            };
            if (codegen_with_natural_lanes_if_necessary(op->type, op->args, cg_func)) {
                return;
            }
        } else if (op->is_intrinsic(Call::shift_left)) {
            if (op->args[1].type().is_uint()) {
                codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
                    internal_assert(args.size() == 2);
                    return builder->CreateShl(args[0], args[1]);
                };
                if (codegen_with_natural_lanes_if_necessary(op->type, op->args, cg_func)) {
                    return;
                }
            } else {
                value = codegen(lower_signed_shift_left(op->args[0], op->args[1]));
            }
        } else if (op->is_intrinsic(Call::shift_right)) {
            if (op->args[1].type().is_uint()) {
                codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
                    internal_assert(args.size() == 2);
                    if (op->type.is_int()) {
                        return builder->CreateAShr(args[0], args[1]);
                    } else {
                        return builder->CreateLShr(args[0], args[1]);
                    }
                };
                if (codegen_with_natural_lanes_if_necessary(op->type, op->args, cg_func)) {
                    return;
                }
            } else {
                value = codegen(lower_signed_shift_right(op->args[0], op->args[1]));
            }
        } else if (op->is_intrinsic(Call::div_round_to_zero)) {
            // See if we can rewrite it to something faster (e.g. a shift)
            Expr e = lower_int_uint_div(op->args[0], op->args[1], /** round to zero */ true);
            if (!e.as<Call>()) {
                codegen(e);
                return;
            }

            internal_assert(op->type.is_int_or_uint());
            codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
                internal_assert(args.size() == 2);
                if (op->type.is_int()) {
                    return builder->CreateSDiv(args[0], args[1]);
                } else {
                    return builder->CreateUDiv(args[0], args[1]);
                }
            };
            if (codegen_with_natural_lanes_if_necessary(op->type, op->args, cg_func)) {
                return;
            }
        } else if (op->is_intrinsic(Call::mod_round_to_zero)) {
            internal_assert(op->type.is_int_or_uint());
            codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
                internal_assert(args.size() == 2);
                if (op->type.is_int()) {
                    return builder->CreateSRem(args[0], args[1]);
                } else {
                    return builder->CreateURem(args[0], args[1]);
                }
            };
            if (codegen_with_natural_lanes_if_necessary(op->type, op->args, cg_func)) {
                return;
            }
        } else if (op->is_intrinsic(Call::popcount)) {
            codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
                internal_assert(args.size() == 1);
                llvm::Function *fn = llvm::Intrinsic::getDeclaration(module.get(), llvm::Intrinsic::ctpop, {args[0]->getType()});
                return builder->CreateCall(fn, args[0]);
            };
            if (codegen_with_natural_lanes_if_necessary(op->args[0].type(), op->args, cg_func)) {
                return;
            }
        } else if (op->is_intrinsic(Call::count_leading_zeros) ||
                   op->is_intrinsic(Call::count_trailing_zeros)) {

            codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
                internal_assert(args.size() == 1);
                auto intrin_id = (op->is_intrinsic(Call::count_leading_zeros)) ? llvm::Intrinsic::ctlz : llvm::Intrinsic::cttz;
                llvm::Function *fn = llvm::Intrinsic::getDeclaration(module.get(), intrin_id, {args[0]->getType()});
                llvm::Value *is_const_zero_undef = llvm::ConstantInt::getFalse(*context);
                return builder->CreateCall(fn, {args[0], is_const_zero_undef});
            };
            if (codegen_with_natural_lanes_if_necessary(op->args[0].type(), op->args, cg_func)) {
                return;
            }
        } else if (op->call_type == Call::PureExtern &&
                   (op->name == "is_nan_f32" || op->name == "is_nan_f64" || op->name == "is_nan_f16")) {

            // Copied from CodeGen_LLVM::visit(const Call)
            IRBuilder<llvm::ConstantFolder, llvm::IRBuilderDefaultInserter>::FastMathFlagGuard guard(*builder);
            llvm::FastMathFlags safe_flags;
            safe_flags.clear();
            builder->setFastMathFlags(safe_flags);
            builder->setDefaultFPMathTag(strict_fp_math_md);

            codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
                internal_assert(args.size() == 1);
                return builder->CreateFCmpUNO(args[0], args[0]);
            };
            if (codegen_with_natural_lanes_if_necessary(op->args[0].type(), op->args, cg_func)) {
                return;
            }
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const EQ *op) {
    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
        internal_assert(args.size() == 2);
        if (op->a.type().is_float()) {
            return builder->CreateFCmpOEQ(args[0], args[1]);
        } else {
            return builder->CreateICmpEQ(args[0], args[1]);
        }
    };
    if (codegen_with_natural_lanes_if_necessary(op->a.type(), {op->a, op->b}, cg_func)) {
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const NE *op) {
    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
        internal_assert(args.size() == 2);
        if (op->a.type().is_float()) {
            return builder->CreateFCmpONE(args[0], args[1]);
        } else {
            return builder->CreateICmpNE(args[0], args[1]);
        }
    };
    if (codegen_with_natural_lanes_if_necessary(op->a.type(), {op->a, op->b}, cg_func)) {
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const LT *op) {
    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
        internal_assert(args.size() == 2);
        if (op->a.type().is_float()) {
            return builder->CreateFCmpOLT(args[0], args[1]);
        } else if (op->a.type().is_int()) {
            return builder->CreateICmpSLT(args[0], args[1]);
        } else {
            return builder->CreateICmpULT(args[0], args[1]);
        }
    };
    if (codegen_with_natural_lanes_if_necessary(op->a.type(), {op->a, op->b}, cg_func)) {
        return;
    }

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
    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
        internal_assert(args.size() == 2);
        if (op->a.type().is_float()) {
            return builder->CreateFCmpOLE(args[0], args[1]);
        } else if (op->a.type().is_int()) {
            return builder->CreateICmpSLE(args[0], args[1]);
        } else {
            return builder->CreateICmpULE(args[0], args[1]);
        }
    };
    if (codegen_with_natural_lanes_if_necessary(op->a.type(), {op->a, op->b}, cg_func)) {
        return;
    }

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

void CodeGen_ARM::visit(const GT *op) {
    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
        internal_assert(args.size() == 2);
        if (op->a.type().is_float()) {
            return builder->CreateFCmpOGT(args[0], args[1]);
        } else if (op->a.type().is_int()) {
            return builder->CreateICmpSGT(args[0], args[1]);
        } else {
            return builder->CreateICmpUGT(args[0], args[1]);
        }
    };
    if (codegen_with_natural_lanes_if_necessary(op->a.type(), {op->a, op->b}, cg_func)) {
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const GE *op) {
    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
        internal_assert(args.size() == 2);
        if (op->a.type().is_float()) {
            return builder->CreateFCmpOGE(args[0], args[1]);
        } else if (op->a.type().is_int()) {
            return builder->CreateICmpSGE(args[0], args[1]);
        } else {
            return builder->CreateICmpUGE(args[0], args[1]);
        }
    };
    if (codegen_with_natural_lanes_if_necessary(op->a.type(), {op->a, op->b}, cg_func)) {
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const And *op) {
    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    if (try_to_fold_vector_reduce<And>(op->a, op->b)) {
        return;
    }

    if (op->a.type().bits() == 64) {
        codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
            internal_assert(args.size() == 2);
            return builder->CreateAnd(args[0], args[1]);
        };
        if (codegen_with_natural_lanes_if_necessary(op->a.type(), {op->a, op->b}, cg_func)) {
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Or *op) {
    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    if (try_to_fold_vector_reduce<Or>(op->a, op->b)) {
        return;
    }

    if (op->a.type().bits() == 64) {
        codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
            internal_assert(args.size() == 2);
            return builder->CreateOr(args[0], args[1]);
        };
        if (codegen_with_natural_lanes_if_necessary(op->a.type(), {op->a, op->b}, cg_func)) {
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Not *op) {
    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    if (op->a.type().bits() == 64) {
        codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
            internal_assert(args.size() == 1);
            return builder->CreateNot(args[0]);
        };
        if (codegen_with_natural_lanes_if_necessary(op->a.type(), {op->a}, cg_func)) {
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Select *op) {
    // TODO: Workaround for LLVM Error as of LLVM 14
    // https://github.com/llvm/llvm-project/issues/54424
    // https://github.com/llvm/llvm-project/issues/54423
    if (op->type.is_float() || op->type.bits() == 64) {
        codegen_func_t cg_func = [&](int lanes, const std::vector<Value *> &args) {
            internal_assert(args.size() == 3);
            return builder->CreateSelect(args[0], args[1], args[2]);
        };
        if (codegen_with_natural_lanes_if_necessary(op->true_value.type(),
                                                    {op->condition, op->true_value, op->false_value}, cg_func)) {
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Shuffle *op) {
    if (effective_vscale == 0) {
        CodeGen_Posix::visit(op);
        return;
    }

    if (op->is_concat() && op->vectors.size() == 2) {
        // Here, we deal with some specific patterns of concat(a, b).
        // Others are decomposed by CodeGen_LLVM at first,
        // which in turn calles CodeGen_ARM::concat_scalable_vectors().

        if (op->type.bits() == 1 && op->type.is_int_or_uint()) {
            // Predicate vector

            // Peep-hole pattern that matches SVE "whilelt" which represents particular pattern of
            // vector predicate. e.g. 11100000 (active_lanes=3, all_lanes=8)
            if (const int total_lanes = op->type.lanes();
                is_power_of_two(total_lanes) && total_lanes >= 2 && total_lanes <= 16 &&
                is_const_one(op->vectors[0]) && is_const_zero(op->vectors[1])) {

                int active_lanes = op->vectors[0].type().lanes();
                value = codegen_whilelt(op->type.lanes(), 0, active_lanes);
                return;
            } else {
                // Rewrite to process predicate as 8 bit vector
                auto upgrade = [&](int idx) -> Expr {
                    return Cast::make(op->vectors[idx].type().with_bits(8), op->vectors[idx]);
                };
                Expr equiv = Cast::make(op->type, Shuffle::make_concat({upgrade(0), upgrade(1)}));
                equiv = common_subexpression_elimination(equiv);
                value = codegen(equiv);
                return;
            }
        } else if (const Broadcast *bc_1 = op->vectors[1].as<Broadcast>()) {
            // Common pattern where padding is appended to align lanes.
            // Create broadcast of padding with dst lanes, then insert vec[0] at lane 0.
            const int lanes_0 = op->vectors[0].type().lanes();
            const int lanes_1 = op->vectors[1].type().lanes();
            const int total_lanes = lanes_0 + lanes_1;
            const int natural_lanes = target.natural_vector_size(op->type);
            if ((lanes_0 % natural_lanes == 0 && lanes_1 == natural_lanes)) {
                // Safe to use llvm vanilla intrinsic
                Value *val_0 = codegen(op->vectors[0]);
                Value *val_1_scalar = codegen(bc_1->value);
                Value *padding = builder->CreateVectorSplat(element_count(total_lanes), val_1_scalar);
                value = insert_scalable_vector(padding, val_0, 0);
                return;
            } else if (total_lanes <= natural_lanes) {
                // Codegen with arm intrinsic, SEL and WHILELT
                Value *val_0 = codegen(op->vectors[0]);
                Value *val_1_scalar = codegen(bc_1->value);
                Value *padding = builder->CreateVectorSplat(element_count(natural_lanes), val_1_scalar);

                Value *pred = codegen_whilelt(natural_lanes, 0, lanes_0);
                Value *val_0_aligned = extend_scalable_vector(val_0, natural_lanes);

                std::ostringstream instr;
                instr << "llvm.aarch64.sve.sel.nxv"
                      << natural_lanes
                      << (op->type.is_int_or_uint() ? "i" : "f")
                      << op->type.bits();
                auto *vec_type = padding->getType();
                std::vector llvm_arg_types{pred->getType(), vec_type, vec_type};
                llvm::FunctionType *fn_type = FunctionType::get(vec_type, llvm_arg_types, false);
                FunctionCallee fn = module->getOrInsertFunction(instr.str(), fn_type);
                value = builder->CreateCall(fn, {pred, val_0_aligned, padding});
                value = shorten_scalable_vector(value, total_lanes);
                return;
            }
        }
    } else if (op->is_slice()) {

        if (op->slice_stride() == 1 || op->type.lanes() == 1) {
            Value *val = codegen(op->vectors[0]);
            value = slice_scalable_vector(val, op->slice_begin(), op->type.lanes());
            return;
        }

        if (op->type.bits() == 1) {
            // Rewrite to process predicate as 8 bit vector
            auto upgrade = [&](int idx) -> Expr {
                return Cast::make(op->vectors[idx].type().with_bits(8), op->vectors[idx]);
            };
            Expr equiv = Shuffle::make_slice(upgrade(0), op->slice_begin(), op->slice_stride(), op->type.lanes());
            equiv = Cast::make(op->type, equiv);
            equiv = common_subexpression_elimination(equiv);
            value = codegen(equiv);
            return;
        }
    } else if (op->is_broadcast()) {
        Expr equiv;
        for (int f = 0; f < op->broadcast_factor(); ++f) {
            if (equiv.defined()) {
                equiv = Shuffle::make_concat({equiv, op->vectors[0]});
            } else {
                equiv = op->vectors[0];
            }
        }
        equiv = common_subexpression_elimination(equiv);
        value = codegen(equiv);
        return;
    } else if (op->is_interleave()) {
        if (op->type.bits() == 1) {
            // Rewrite to process bool vector as 8 bit vector
            std::vector<Expr> vecs_i8;
            Type upgraded_type = op->vectors[0].type().with_bits(8);
            for (const auto &vec_i1 : op->vectors) {
                vecs_i8.emplace_back(Cast::make(upgraded_type, vec_i1));
            }
            Expr equiv = Shuffle::make_interleave(vecs_i8);
            equiv = Cast::make(op->type, equiv);
            equiv = common_subexpression_elimination(equiv);
            value = codegen(equiv);
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

Value *CodeGen_ARM::interleave_vectors(const std::vector<Value *> &vecs) {
    if (neon_intrinsics_disabled()) {
        return CodeGen_Posix::interleave_vectors(vecs);
    }

    if (target.has_feature(Target::SVE2)) {
        if (vecs.size() == 2) {
            // ZIP1/ZIP2
            llvm::Type *vt = vecs[0]->getType();
            llvm::Type *elt = get_vector_element_type(vt);
            const int bits = elt->getScalarSizeInBits();
            const int src_lanes = get_vector_num_elements(vt);
            const int dst_lanes = src_lanes * 2;
            const int natural_lanes = target.natural_vector_size(Int(bits));
            llvm::Type *vt_natural = get_vector_type(elt, natural_lanes);

            llvm::FunctionType *fn_type = FunctionType::get(vt_natural, {vt_natural, vt_natural}, false);
            std::ostringstream mangle;
            mangle << ".nxv"
                   << natural_lanes
                   << (elt->isIntegerTy() ? "i" : "f")
                   << bits;
            FunctionCallee fn_zip1 = module->getOrInsertFunction("llvm.aarch64.sve.zip1" + mangle.str(), fn_type);
            FunctionCallee fn_zip2 = module->getOrInsertFunction("llvm.aarch64.sve.zip2" + mangle.str(), fn_type);

            std::vector<Value *> results;
            for (int i = 0; i < src_lanes; i += natural_lanes) {
                const int active_src_lanes = std::min(src_lanes - i, natural_lanes);
                const int active_dst_lanes = active_src_lanes * 2;
                Value *src_0 = slice_vector(vecs[0], i, natural_lanes);
                Value *src_1 = slice_vector(vecs[1], i, natural_lanes);

                // ZIP1 for high halves
                Value *slice_hi = builder->CreateCall(fn_zip1, {src_0, src_1});
                results.push_back(slice_hi);

                // ZIP2 for low halves
                if (active_dst_lanes > natural_lanes) {
                    Value *slice_low = builder->CreateCall(fn_zip2, {src_0, src_1});
                    results.push_back(slice_low);
                }
            }
            Value *result = concat_vectors(results);
            result = slice_vector(result, 0, dst_lanes);
            return result;
        }
    }

    return CodeGen_Posix::interleave_vectors(vecs);
};

Value *CodeGen_ARM::shuffle_vectors(Value *a, Value *b, const std::vector<int> &indices) {
    if (neon_intrinsics_disabled()) {
        return CodeGen_Posix::shuffle_vectors(a, b, indices);
    }

    if (target.has_feature(Target::SVE2)) {
        internal_assert(a->getType() == b->getType());
        if (!a->getType()->isVectorTy()) {
            a = create_broadcast(a, 1);
            b = create_broadcast(b, 1);
        }

        internal_assert(is_scalable_vector(a));
        llvm::Type *elt = get_vector_element_type(a->getType());
        const int bits = elt->getScalarSizeInBits();
        const int natural_lanes = target.natural_vector_size(Int(bits));
        const int src_lanes = get_vector_num_elements(a->getType());
        const int dst_lanes = indices.size();

        // SVE intrinsic can only deal with vector shorter than natural_lanes
        if (src_lanes <= natural_lanes && dst_lanes <= natural_lanes) {
            if (*std::max_element(indices.begin(), indices.end()) < src_lanes) {
                // Only a is necessary. i.e. b is unused.
                return shuffle_single_vector_with_tbl(a, indices);
            } else {
                return shuffle_vectors_with_tbl2(a, b, indices);
            }
        } else if (Value *v = try_to_decompose_into_sub_shuffles(a, b, indices)) {
            return v;
        }

        // Scalarize, as there is no intrinsic for general shuffle for scalable vector as of LLVM 14
        const int arg_lanes = get_vector_num_elements(a->getType());
        llvm::Type *dst_type = get_vector_type(a->getType()->getScalarType(), indices.size());
        Value *dst = UndefValue::get(dst_type);
        for (size_t i = 0; i < indices.size(); ++i) {
            Value *src = a;
            Value *index = ConstantInt::get(i32_t, indices[i]);
            if (indices[i] >= arg_lanes) {
                src = b;
                index = ConstantInt::get(i32_t, indices[i] - arg_lanes);
            } else if (indices[i] == -1) {
                index = UndefValue::get(i32_t);
            }
            Value *extracted = builder->CreateExtractElement(src, index);
            dst = builder->CreateInsertElement(dst, extracted, i);
        }
        return dst;
    }

    return CodeGen_Posix::shuffle_vectors(a, b, indices);
}

Value *CodeGen_ARM::shuffle_single_vector_with_tbl(Value *a, const std::vector<int> &indices) {
    internal_assert(target.has_feature(Target::SVE2));
    llvm::Type *elt = get_vector_element_type(a->getType());
    const int bits = elt->getScalarSizeInBits();
    const int natural_lanes = target.natural_vector_size(Int(bits));
    const int src_lanes = get_vector_num_elements(a->getType());
    const int dst_lanes = indices.size();
    internal_assert(src_lanes <= natural_lanes && dst_lanes <= natural_lanes);

    if (src_lanes < natural_lanes) {
        a = slice_vector(a, 0, natural_lanes);
    }
    std::ostringstream instr;
    instr << "llvm.aarch64.sve.tbl.nxv"
          << natural_lanes
          << (elt->isIntegerTy() ? "i" : "f")
          << bits;

    std::vector<int> new_indices(natural_lanes, -1);
    for (size_t i = 0; i < indices.size(); ++i) {
        new_indices[i] = indices[i];
    }
    Value *val_indices = codegen_shuffle_indices(bits, new_indices);

    llvm::Type *vt_natural = get_vector_type(elt, natural_lanes);
    std::vector llvm_arg_types{vt_natural, val_indices->getType()};
    llvm::FunctionType *fn_type = FunctionType::get(vt_natural, llvm_arg_types, false);
    FunctionCallee fn = module->getOrInsertFunction(instr.str(), fn_type);

    Value *v = builder->CreateCall(fn, {a, val_indices});
    v = slice_vector(v, 0, dst_lanes);
    return v;
}

Value *CodeGen_ARM::shuffle_vectors_with_tbl2(Value *a, Value *b, const std::vector<int> &indices) {
    internal_assert(target.has_feature(Target::SVE2));
    internal_assert(a->getType() == b->getType());
    llvm::Type *elt = get_vector_element_type(a->getType());
    const int bits = elt->getScalarSizeInBits();
    const int natural_lanes = target.natural_vector_size(Int(bits));
    const int src_lanes = get_vector_num_elements(a->getType());
    const int dst_lanes = indices.size();
    internal_assert(src_lanes <= natural_lanes && dst_lanes <= natural_lanes);

    if (src_lanes < natural_lanes) {
        a = slice_vector(a, 0, natural_lanes);
        b = slice_vector(b, 0, natural_lanes);
    }
    std::ostringstream instr;
    instr << "llvm.aarch64.sve.tbl2.nxv"
          << natural_lanes
          << (elt->isIntegerTy() ? "i" : "f")
          << bits;

    std::vector<int> new_indices(natural_lanes, -1);
    for (size_t i = 0; i < indices.size(); ++i) {
        int index = indices[i];
        if (index >= src_lanes) {
            // The index originaly pointed to "b".
            // Now that "a" is aligned to natural_lanes, we need to add offset.
            index += (natural_lanes - src_lanes);
        }
        new_indices[i] = index;
    }
    Value *val_indices = codegen_shuffle_indices(bits, new_indices);

    llvm::Type *vt_natural = get_vector_type(elt, natural_lanes);
    std::vector llvm_arg_types{vt_natural, vt_natural, val_indices->getType()};
    llvm::FunctionType *fn_type = FunctionType::get(vt_natural, llvm_arg_types, false);
    FunctionCallee fn = module->getOrInsertFunction(instr.str(), fn_type);

    Value *v = builder->CreateCall(fn, {a, b, val_indices});
    v = slice_vector(v, 0, dst_lanes);
    return v;
}

Value *CodeGen_ARM::codegen_shuffle_indices(int bits, const std::vector<int> &indices) {
    const int lanes = indices.size();
    llvm::Type *index_type = IntegerType::get(module->getContext(), bits);
    llvm::Type *index_vec_type = get_vector_type(index_type, lanes);

    std::vector<Constant *> llvm_indices(lanes);
    for (int i = 0; i < lanes; i++) {
        int idx = indices[i];
        llvm_indices[i] = idx >= 0 ? ConstantInt::get(index_type, idx) : UndefValue::get(index_type);
    }
    Value *v = ConstantVector::get(llvm_indices);
    // Convert fixed sized vector into scalable vector.
    // As of LLVM 14, this works only with natural_lanes.
    // https://github.com/llvm/llvm-project/issues/55412
    internal_assert(lanes == target.natural_vector_size(Int(bits)));
    v = builder->CreateInsertVector(index_vec_type, UndefValue::get(index_vec_type),
                                    v, ConstantInt::get(i64_t, 0));
    return v;
}

Value *CodeGen_ARM::try_to_decompose_into_sub_shuffles(Value *a, Value *b, const std::vector<int> &org_indices) {
    internal_assert(a->getType() == b->getType());
    llvm::Type *elt = get_vector_element_type(a->getType());
    const int bits = elt->getScalarSizeInBits();
    const int natural_lanes = target.natural_vector_size(Int(bits));
    const int src_lanes = get_vector_num_elements(a->getType());
    const int dst_lanes = org_indices.size();

    // Update indices so that src vectors are aligned with natural_lanes
    const int src_lanes_aligned = align_up(src_lanes, natural_lanes);
    const int num_slices_a = src_lanes_aligned / natural_lanes;
    const int num_slices = num_slices_a * 2;
    std::vector<int> indices = org_indices;
    for (auto &idx : indices) {
        if (idx >= src_lanes) {
            // This idx points to src b. Adjust as if src a is aligned.
            idx += src_lanes_aligned - src_lanes;
        }
    }

    struct Slice {
        Value *base;  // vector from which slice is created
        int start;

        // Track which index of shuffle uses this slice as src.
        // Collect index to indices[] as relation.
        std::set<int> indices_to_shuffle_indices;

        Slice(Value *b, int s)
            : base(b), start(s){};
    };
    std::vector<Slice> src_slices;

    // Initialize src_slices
    for (int i = 0; i < num_slices; ++i) {
        if (i < num_slices_a) {
            src_slices.emplace_back(a, i * natural_lanes);
        } else {
            src_slices.emplace_back(b, (i - num_slices_a) * natural_lanes);
        }
    }

    struct SubShuffle {
        std::vector<Slice> src_vectors;
        std::vector<int> indices;
    };
    std::vector<SubShuffle> sub_shuffles;

    // Try to decompose into sub shuffles where src vectors have natural_lanes
    bool possible = true;
    for (int sub_dst_start = 0; sub_dst_start < dst_lanes; sub_dst_start += natural_lanes) {
        // reset indices of previous iteration
        for (auto &s : src_slices) {
            s.indices_to_shuffle_indices.clear();
        }
        const int dst_slice_size = std::min(dst_lanes - sub_dst_start, natural_lanes);
        std::vector<int> local_indices(dst_slice_size);

        for (int i = 0; i < dst_slice_size; ++i) {
            int index = indices[sub_dst_start + i];

            if (index < 0) {
                local_indices[i] = index;
            } else {
                local_indices[i] = index % natural_lanes;
                // Track which src slice this index points to.
                int slice_index = index / natural_lanes;
                internal_assert(slice_index < num_slices);
                src_slices[slice_index].indices_to_shuffle_indices.insert(i);
            }
        }

        // Collect slices which is actually accessed by this sub shuffle
        std::vector<Slice> used_slices;
        for (auto &s : src_slices) {
            if (!s.indices_to_shuffle_indices.empty()) {
                used_slices.push_back(s);
            }
        }

        // if the number of slices used for this sub shuffle is 2 or less, it is possible to use intrin
        if (used_slices.size() > 2) {
            possible = false;
            break;
        }

        SubShuffle sub_shuffle{used_slices, local_indices};
        if (used_slices.size() == 2) {
            // Add offset to the index which points to the 2nd src slice
            for (auto i : used_slices[1].indices_to_shuffle_indices) {
                sub_shuffle.indices[i] += natural_lanes;
            }
        }
        sub_shuffles.emplace_back(std::move(sub_shuffle));
    }

    if (possible) {
        // Now generate all the sub shuffles
        std::vector<Value *> val_sub_shuffles;
        for (auto &ss : sub_shuffles) {
            Value *val_ss_a = UndefValue::get(get_vector_type(elt, natural_lanes));
            Value *val_ss_b = UndefValue::get(get_vector_type(elt, natural_lanes));
            if (!ss.src_vectors.empty()) {
                val_ss_a = slice_vector(ss.src_vectors[0].base, ss.src_vectors[0].start, natural_lanes);
            }
            if (ss.src_vectors.size() == 2) {
                val_ss_b = slice_vector(ss.src_vectors[1].base, ss.src_vectors[1].start, natural_lanes);
            }
            Value *val_ss = shuffle_vectors(val_ss_a, val_ss_b, ss.indices);
            val_sub_shuffles.push_back(val_ss);
        }
        // Retrieve origial lanes
        Value *dst = concat_vectors(val_sub_shuffles);
        dst = slice_vector(dst, 0, dst_lanes);
        return dst;
    }

    return nullptr;
}

void CodeGen_ARM::codegen_vector_reduce(const VectorReduce *op, const Expr &init) {
    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::codegen_vector_reduce(op, init);
        return;
    }

    if (codegen_dot_product_vector_reduce(op, init)) {
        return;
    }
    if (codegen_pairwise_vector_reduce(op, init)) {
        return;
    }
    if (codegen_across_vector_reduce(op, init)) {
        return;
    }
    CodeGen_Posix::codegen_vector_reduce(op, init);
}

bool CodeGen_ARM::codegen_dot_product_vector_reduce(const VectorReduce *op, const Expr &init) {
    if (op->op != VectorReduce::Add) {
        return false;
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
        {VectorReduce::Add, 4, i64(widening_mul(wild_i16x_, wild_i16x_)), "dot_product", Target::SVE2},
        {VectorReduce::Add, 4, i64(widening_mul(wild_u16x_, wild_u16x_)), "dot_product", Target::SVE2},
        {VectorReduce::Add, 4, u64(widening_mul(wild_u16x_, wild_u16x_)), "dot_product", Target::SVE2},
        // A sum is the same as a dot product with a vector of ones, and this appears to
        // be a bit faster.
        {VectorReduce::Add, 4, i32(wild_i8x_), "dot_product", Target::ARMDotProd, {1}},
        {VectorReduce::Add, 4, i32(wild_u8x_), "dot_product", Target::ARMDotProd, {1}},
        {VectorReduce::Add, 4, u32(wild_u8x_), "dot_product", Target::ARMDotProd, {1}},
        {VectorReduce::Add, 4, i64(wild_i16x_), "dot_product", Target::SVE2, {1}},
        {VectorReduce::Add, 4, i64(wild_u16x_), "dot_product", Target::SVE2, {1}},
        {VectorReduce::Add, 4, u64(wild_u16x_), "dot_product", Target::SVE2, {1}},
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
                return true;
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
            if (Value *v = call_overloaded_intrin(op->type, p.intrin, {i, matches[0], matches[1]})) {
                value = v;
                return true;
            }
        }
    }

    return false;
}

bool CodeGen_ARM::codegen_pairwise_vector_reduce(const VectorReduce *op, const Expr &init) {
    if (op->op != VectorReduce::Add &&
        op->op != VectorReduce::Max &&
        op->op != VectorReduce::Min) {
        return false;
    }

    // TODO: Move this to be patterns? The patterns are pretty trivial, but some
    // of the other logic is tricky.
    int factor = op->value.type().lanes() / op->type.lanes();
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
            if (init.defined() && (target.bits == 32 || target.has_feature(Target::SVE2))) {
                // On 32-bit or SVE2, we have an intrinsic for widening add-accumulate.
                intrin = "pairwise_widening_add_accumulate";
                intrin_args = {accumulator, narrow};
                accumulator = Expr();
            } else if (target.has_feature(Target::SVE2)) {
                intrin = "pairwise_widening_add_accumulate";
                intrin_args = {Expr(0), narrow};
                accumulator = Expr();
            } else {
                // On 64-bit, LLVM pattern matches widening add-accumulate if
                // we give it the widening add.
                intrin = "pairwise_widening_add";
                intrin_args = {narrow};
            }
        } else if (!target.has_feature(Target::SVE2)) {
            // Exclude SVE, as it process lanes in different order (even/odd wise) than NEON
            intrin = "pairwise_add";
            intrin_args = {op->value};
        }
    } else if (op->op == VectorReduce::Min && factor == 2 && !target.has_feature(Target::SVE2)) {
        intrin = "pairwise_min";
        intrin_args = {op->value};
    } else if (op->op == VectorReduce::Max && factor == 2 && !target.has_feature(Target::SVE2)) {
        intrin = "pairwise_max";
        intrin_args = {op->value};
    }

    if (intrin) {
        if (Value *v = call_overloaded_intrin(op->type, intrin, intrin_args)) {
            value = v;
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
            return true;
        }
    }

    return false;
}

bool CodeGen_ARM::codegen_across_vector_reduce(const VectorReduce *op, const Expr &init) {
    if (effective_vscale == 0) {
        // Leave this to vanilla codegen to emit "llvm.vector.reduce." intrinsic,
        // which doesn't support scalable vector in LLVM 14
        return false;
    }

    if (op->op != VectorReduce::Add &&
        op->op != VectorReduce::Max &&
        op->op != VectorReduce::Min) {
        return false;
    }

    Expr val = op->value;
    const int output_lanes = op->type.lanes();
    const int native_lanes = target.natural_vector_size(op->type);
    const int input_lanes = val.type().lanes();
    const int input_bits = op->type.bits();
    Type elt = op->type.element_of();

    if (output_lanes != 1 || input_lanes < 2) {
        return false;
    }

    Expr (*binop)(Expr, Expr) = nullptr;
    std::string op_name;
    switch (op->op) {
    case VectorReduce::Add:
        binop = Add::make;
        op_name = "add";
        break;
    case VectorReduce::Min:
        binop = Min::make;
        op_name = "min";
        break;
    case VectorReduce::Max:
        binop = Max::make;
        op_name = "max";
        break;
    default:
        internal_error << "unreachable";
    }

    if (input_lanes == native_lanes) {
        std::stringstream name;  // e.g. llvm.aarch64.sve.sminv.nxv4i32
        name << "llvm.aarch64.sve."
             << (op->type.is_float() ? "f" : op->type.is_int() ? "s" :
                                                                 "u")
             << op_name << "v"
             << ".nxv" << native_lanes << (op->type.is_float() ? "f" : "i") << input_bits;

        // Integer add accumulation output is 64 bit only
        const bool type_upgraded = op->op == VectorReduce::Add && op->type.is_int_or_uint();
        const int output_bits = type_upgraded ? 64 : input_bits;
        Type intrin_ret_type = op->type.with_bits(output_bits);

        const string intrin_name = name.str();

        Expr pred = const_true(native_lanes);
        vector<Expr> args{pred, op->value};

        // Make sure the declaration exists, or the codegen for
        // call will assume that the args should scalarize.
        if (!module->getFunction(intrin_name)) {
            vector<llvm::Type *> arg_types;
            for (const Expr &e : args) {
                arg_types.push_back(llvm_type_of(e.type()));
            }
            FunctionType *func_t = FunctionType::get(llvm_type_of(intrin_ret_type), arg_types, false);
            llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, intrin_name, module.get());
        }

        Expr equiv = Call::make(intrin_ret_type, intrin_name, args, Call::PureExtern);
        if (type_upgraded) {
            equiv = Cast::make(op->type, equiv);
        }
        if (init.defined()) {
            equiv = binop(init, equiv);
        }
        equiv = common_subexpression_elimination(equiv);
        equiv.accept(this);
        return true;

    } else if (input_lanes < native_lanes) {
        // Create equivalent where lanes==native_lanes by padding data which doesn't affect the result
        Expr padding;
        const int inactive_lanes = native_lanes - input_lanes;

        switch (op->op) {
        case VectorReduce::Add:
            padding = make_zero(elt.with_lanes(inactive_lanes));
            break;
        case VectorReduce::Min:
            padding = elt.with_lanes(inactive_lanes).min();
            break;
        case VectorReduce::Max:
            padding = elt.with_lanes(inactive_lanes).max();
            break;
        default:
            internal_error << "unreachable";
        }

        Expr equiv = VectorReduce::make(op->op, Shuffle::make_concat({val, padding}), 1);
        if (init.defined()) {
            equiv = binop(equiv, init);
        }
        equiv = common_subexpression_elimination(equiv);
        equiv.accept(this);
        return true;
    }

    return false;
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

Value *CodeGen_ARM::codegen_whilelt(int total_lanes, int start, int end) {
    // Generates SVE "whilelt" instruction which represents vector predicate pattern of
    // e.g. 11100000 (total_lanes = 8 , start = 0, end = 3)
    //     -> @llvm.aarch64.sve.whilelt.nxv8i1.i32(i32 0, i32 3)
    std::ostringstream instr;
    instr << "llvm.aarch64.sve.whilelt.nxv"
          << total_lanes
          << "i1.i32";
    llvm::Type *pred_type = get_vector_type(llvm_type_of(Int(1)), total_lanes);

    llvm::FunctionType *fn_type = FunctionType::get(pred_type, {i32_t, i32_t}, false);
    FunctionCallee fn = module->getOrInsertFunction(instr.str(), fn_type);

    value = builder->CreateCall(fn, {codegen(Expr(start)), codegen(Expr(end))});

    return value;
}

bool CodeGen_ARM::codegen_with_natural_lanes_if_necessary(Type op_type, const std::vector<Expr> &args, codegen_func_t &cg_func) {
    if (effective_vscale != 0 &&
        op_type.is_vector() &&
        !is_power_of_two(op_type.lanes())) {  // This condition is to avoid LLVM Error as of LLVM 14.

        value = codegen_with_lanes(target.natural_vector_size(op_type), op_type.lanes(), args, cg_func);
        return true;
    }
    return false;
}

Value *CodeGen_ARM::codegen_with_lanes(int slice_lanes, int total_lanes,
                                       const std::vector<Expr> &args, codegen_func_t &cg_func) {
    std::vector<Value *> llvm_args;
    // codegen args
    for (const auto &arg : args) {
        llvm_args.push_back(codegen(arg));
    }

    if (slice_lanes == total_lanes) {
        // codegen op
        return cg_func(slice_lanes, llvm_args);
    }

    std::vector<Value *> results;
    for (int start = 0; start < total_lanes; start += slice_lanes) {
        std::vector<Value *> sliced_args;
        for (auto &llvm_arg : llvm_args) {
            Value *v = llvm_arg;
            if (get_vector_num_elements(llvm_arg->getType()) == total_lanes) {
                // Except for scalar argument which some ops have, arguments are sliced
                v = slice_vector(llvm_arg, start, slice_lanes);
            }
            sliced_args.push_back(v);
        }
        // codegen op
        value = cg_func(slice_lanes, sliced_args);
        results.push_back(value);
    }
    // Restore the results into vector with total_lanes
    value = concat_vectors(results);
    return slice_vector(value, 0, total_lanes);
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
