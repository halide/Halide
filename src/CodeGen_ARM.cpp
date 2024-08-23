#include <set>
#include <sstream>

#include "CSE.h"
#include "CodeGen_Internal.h"
#include "CodeGen_Posix.h"
#include "ConciseCasts.h"
#include "Debug.h"
#include "DistributeShifts.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IRMutator.h"
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

// Populate feature flags in a target according to those implied by
// existing flags, so that instruction patterns can just check for the
// oldest feature flag that supports an instruction.
//
// According to LLVM, ARM architectures have the following is-a-superset-of
// relationships:
//
//   v9.5a > v9.4a > v9.3a > v9.2a > v9.1a > v9a;
//             v       v       v       v       v
//           v8.9a > v8.8a > v8.7a > v8.6a > v8.5a > v8.4a > ... > v8a;
//
// v8r has no relation to anything.
Target complete_arm_target(Target t) {
    constexpr int num_arm_v8_features = 10;
    static const Target::Feature arm_v8_features[num_arm_v8_features] = {
        Target::ARMv89a,
        Target::ARMv88a,
        Target::ARMv87a,
        Target::ARMv86a,
        Target::ARMv85a,
        Target::ARMv84a,
        Target::ARMv83a,
        Target::ARMv82a,
        Target::ARMv81a,
        Target::ARMv8a,
    };

    for (int i = 0; i < num_arm_v8_features - 1; i++) {
        if (t.has_feature(arm_v8_features[i])) {
            t.set_feature(arm_v8_features[i + 1]);
        }
    }

    return t;
}

// Substitute in loads that feed into slicing shuffles, to help with vld2/3/4
// emission. These are commonly lifted as lets because they get used by multiple
// interleaved slices of the same load.
class SubstituteInStridedLoads : public IRMutator {
    Scope<Expr> loads;
    std::map<std::string, std::vector<std::string>> vars_per_buffer;
    std::set<std::string> poisoned_vars;

    template<typename LetOrLetStmt>
    auto visit_let(const LetOrLetStmt *op) -> decltype(op->body) {
        const Load *l = op->value.template as<Load>();
        const Ramp *r = l ? l->index.as<Ramp>() : nullptr;
        auto body = op->body;
        if (r && is_const_one(r->stride)) {
            ScopedBinding bind(loads, op->name, op->value);
            vars_per_buffer[l->name].push_back(op->name);
            body = mutate(op->body);
            vars_per_buffer[l->name].pop_back();
            poisoned_vars.erase(l->name);
        } else {
            body = mutate(op->body);
        }

        // Unconditionally preserve the let, because there may be unsubstituted uses of
        // it. It'll get dead-stripped by LLVM if not.
        return LetOrLetStmt::make(op->name, op->value, body);
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    // Avoid substituting a load over an intervening store
    Stmt visit(const Store *op) override {
        auto it = vars_per_buffer.find(op->name);
        if (it != vars_per_buffer.end()) {
            for (const auto &v : it->second) {
                poisoned_vars.insert(v);
            }
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Shuffle *op) override {
        int stride = op->slice_stride();
        const Variable *var = op->vectors[0].as<Variable>();
        const Expr *vec = nullptr;
        if (var &&
            poisoned_vars.count(var->name) == 0 &&
            op->vectors.size() == 1 &&
            2 <= stride && stride <= 4 &&
            op->slice_begin() < stride &&
            (vec = loads.find(var->name))) {
            return Shuffle::make_slice({*vec}, op->slice_begin(), op->slice_stride(), op->type.lanes());
        } else {
            return IRMutator::visit(op);
        }
    }

    using IRMutator::visit;
};

/** A code generator that emits ARM code from a given Halide stmt. */
class CodeGen_ARM : public CodeGen_Posix {
public:
    /** Create an ARM code generator for the given arm target. */
    CodeGen_ARM(const Target &);

protected:
    using codegen_func_t = std::function<Value *(int lanes, const std::vector<Value *> &)>;
    using CodeGen_Posix::visit;

    /** Similar to llvm_type_of, but allows providing a VectorTypeConstraint to
     * force Fixed or VScale vector results. */
    llvm::Type *llvm_type_with_constraint(const Type &t, bool scalars_are_vectors, VectorTypeConstraint constraint);

    /** Define a wrapper LLVM func that takes some arguments which Halide defines
     * and call inner LLVM intrinsic with an additional argument which LLVM requires. */
    llvm::Function *define_intrin_wrapper(const std::string &inner_name,
                                          const Type &ret_type,
                                          const std::string &mangled_name,
                                          const std::vector<Type> &arg_types,
                                          int intrinsic_flags,
                                          bool sve_intrinsic);

    void init_module() override;
    void compile_func(const LoweredFunc &f,
                      const std::string &simple_name, const std::string &extern_name) override;

    void begin_func(LinkageType linkage, const std::string &simple_name,
                    const std::string &extern_name, const std::vector<LoweredArgument> &args) override;

    /** Nodes for which we want to emit specific ARM vector intrinsics */
    // @{
    void visit(const Cast *) override;
    void visit(const Add *) override;
    void visit(const Sub *) override;
    void visit(const Min *) override;
    void visit(const Max *) override;
    void visit(const Store *) override;
    void visit(const Load *) override;
    void visit(const Shuffle *) override;
    void visit(const Ramp *) override;
    void visit(const Call *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void codegen_vector_reduce(const VectorReduce *, const Expr &) override;
    bool codegen_dot_product_vector_reduce(const VectorReduce *, const Expr &);
    bool codegen_pairwise_vector_reduce(const VectorReduce *, const Expr &);
    bool codegen_across_vector_reduce(const VectorReduce *, const Expr &);
    // @}
    Type upgrade_type_for_arithmetic(const Type &t) const override;
    Type upgrade_type_for_argument_passing(const Type &t) const override;
    Type upgrade_type_for_storage(const Type &t) const override;

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
    int target_vscale() const override;

    // NEON can be disabled for older processors.
    bool simd_intrinsics_disabled() {
        return target.has_feature(Target::NoNEON) &&
               !target.has_feature(Target::SVE2);
    }

    bool is_float16_and_has_feature(const Type &t) const {
        // NOTE : t.is_float() returns true even in case of BFloat16. We don't include it for now.
        return t.code() == Type::Float && t.bits() == 16 && target.has_feature(Target::ARMFp16);
    }
    bool supports_call_as_float16(const Call *op) const override;

    /** Make predicate vector which starts with consecutive true followed by consecutive false */
    Expr make_vector_predicate_1s_0s(int true_lanes, int false_lanes) {
        internal_assert((true_lanes + false_lanes) != 0) << "CodeGen_ARM::make_vector_predicate_1s_0s called with total of 0 lanes.\n";
        if (true_lanes == 0) {
            return const_false(false_lanes);
        } else if (false_lanes == 0) {
            return const_true(true_lanes);
        } else {
            return Shuffle::make_concat({const_true(true_lanes), const_false(false_lanes)});
        }
    }
};

CodeGen_ARM::CodeGen_ARM(const Target &target)
    : CodeGen_Posix(complete_arm_target(target)) {

    // TODO(https://github.com/halide/Halide/issues/8088): See if
    // use_llvm_vp_intrinsics can replace architecture specific code in this
    // file, specifically in Load and Store visitors.  Depends on quality of
    // LLVM aarch64 backend lowering for these intrinsics on SVE2.

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

    // VCVTP/M
    casts.emplace_back("fp_to_int_floor", i32(floor(wild_f32x_)));
    casts.emplace_back("fp_to_int_floor", u32(floor(wild_f32x_)));
    casts.emplace_back("fp_to_int_ceil", i32(ceil(wild_f32x_)));
    casts.emplace_back("fp_to_int_ceil", u32(ceil(wild_f32x_)));

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
        Neon64Unavailable = 1 << 9,  // Unavailable for 64 bit NEON
        SveUnavailable = 1 << 10,    // Unavailable for SVE
        SveNoPredicate = 1 << 11,    // In SVE intrinsics, additional predicate argument is required as default, unless this flag is set.
        SveInactiveArg = 1 << 12,    // This intrinsic needs the additional argument for fallback value for the lanes inactivated by predicate.
        SveRequired = 1 << 13,       // This intrinsic requires SVE.
    };
};

// clang-format off
const ArmIntrinsic intrinsic_defs[] = {
    // TODO(https://github.com/halide/Halide/issues/8093):
    // Some of the Arm intrinsic have the same name between Neon and SVE2 but with different behavior. For example,
    // widening, narrowing and pair-wise operations which are performed in even (top) and odd (bottom) lanes basis in SVE,
    // while in high and low lanes in Neon. Therefore, peep-hole code-gen with those SVE2 intrinsic is not enabled for now,
    // because additional interleaving/deinterleaveing would be required to restore the element order in a vector.

    {"vabs", "abs", UInt(8, 8), "abs", {Int(8, 8)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveInactiveArg},
    {"vabs", "abs", UInt(16, 4), "abs", {Int(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveInactiveArg},
    {"vabs", "abs", UInt(32, 2), "abs", {Int(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveInactiveArg},
    {"llvm.fabs", "llvm.fabs", Float(16, 4), "abs", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16 | ArmIntrinsic::SveNoPredicate},
    {"llvm.fabs", "llvm.fabs", Float(32, 2), "abs", {Float(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveNoPredicate},
    {"llvm.fabs", "llvm.fabs", Float(64, 2), "abs", {Float(64, 2)},  ArmIntrinsic::SveNoPredicate},
    {"llvm.fabs.f16", "llvm.fabs.f16", Float(16), "abs", {Float(16)}, ArmIntrinsic::RequireFp16 | ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},
    {"llvm.fabs.f32", "llvm.fabs.f32", Float(32), "abs", {Float(32)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},
    {"llvm.fabs.f64", "llvm.fabs.f64", Float(64), "abs", {Float(64)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},

    {"llvm.sqrt", "llvm.sqrt", Float(16, 4), "sqrt_f16", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16 | ArmIntrinsic::SveNoPredicate},
    {"llvm.sqrt", "llvm.sqrt", Float(32, 2), "sqrt_f32", {Float(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveNoPredicate},
    {"llvm.sqrt", "llvm.sqrt", Float(64, 2), "sqrt_f64", {Float(64, 2)}, ArmIntrinsic::SveNoPredicate},
    {"llvm.sqrt.f16", "llvm.sqrt.f16", Float(16), "sqrt_f16", {Float(16)}, ArmIntrinsic::RequireFp16 | ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},
    {"llvm.sqrt.f32", "llvm.sqrt.f32", Float(32), "sqrt_f32", {Float(32)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},
    {"llvm.sqrt.f64", "llvm.sqrt.f64", Float(64), "sqrt_f64", {Float(64)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},

    {"llvm.floor", "llvm.floor", Float(16, 4), "floor_f16", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16 | ArmIntrinsic::SveNoPredicate},
    {"llvm.floor", "llvm.floor", Float(32, 2), "floor_f32", {Float(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveNoPredicate},
    {"llvm.floor", "llvm.floor", Float(64, 2), "floor_f64", {Float(64, 2)}, ArmIntrinsic::SveNoPredicate},
    {"llvm.floor.f16", "llvm.floor.f16", Float(16), "floor_f16", {Float(16)}, ArmIntrinsic::RequireFp16 | ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},
    {"llvm.floor.f32", "llvm.floor.f32", Float(32), "floor_f32", {Float(32)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},
    {"llvm.floor.f64", "llvm.floor.f64", Float(64), "floor_f64", {Float(64)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},

    {"llvm.ceil", "llvm.ceil", Float(16, 4), "ceil_f16", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16 | ArmIntrinsic::SveNoPredicate},
    {"llvm.ceil", "llvm.ceil", Float(32, 2), "ceil_f32", {Float(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveNoPredicate},
    {"llvm.ceil", "llvm.ceil", Float(64, 2), "ceil_f64", {Float(64, 2)}, ArmIntrinsic::SveNoPredicate},
    {"llvm.ceil.f16", "llvm.ceil.f16", Float(16), "ceil_f16", {Float(16)}, ArmIntrinsic::RequireFp16 | ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},
    {"llvm.ceil.f32", "llvm.ceil.f32", Float(32), "ceil_f32", {Float(32)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},
    {"llvm.ceil.f64", "llvm.ceil.f64", Float(64), "ceil_f64", {Float(64)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},

    {"llvm.trunc", "llvm.trunc", Float(16, 4), "trunc_f16", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16 | ArmIntrinsic::SveNoPredicate},
    {"llvm.trunc", "llvm.trunc", Float(32, 2), "trunc_f32", {Float(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveNoPredicate},
    {"llvm.trunc", "llvm.trunc", Float(64, 2), "trunc_f64", {Float(64, 2)}, ArmIntrinsic::SveNoPredicate},
    {"llvm.trunc.f16", "llvm.trunc.f16", Float(16), "trunc_f16", {Float(16)}, ArmIntrinsic::RequireFp16 | ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},
    {"llvm.trunc.f32", "llvm.trunc.f32", Float(32), "trunc_f32", {Float(32)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},
    {"llvm.trunc.f64", "llvm.trunc.f64", Float(64), "trunc_f64", {Float(64)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},

    {"llvm.roundeven", "llvm.roundeven", Float(16, 4), "round", {Float(16, 4)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::RequireFp16 | ArmIntrinsic::SveNoPredicate},
    {"llvm.roundeven", "llvm.roundeven", Float(32, 2), "round", {Float(32, 2)}, ArmIntrinsic::HalfWidth | ArmIntrinsic::SveNoPredicate},
    {"llvm.roundeven", "llvm.roundeven", Float(64, 2), "round", {Float(64, 2)}, ArmIntrinsic::SveNoPredicate},
    {"llvm.roundeven.f16", "llvm.roundeven.f16", Float(16), "round", {Float(16)}, ArmIntrinsic::RequireFp16 | ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},
    {"llvm.roundeven.f32", "llvm.roundeven.f32", Float(32), "round", {Float(32)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},
    {"llvm.roundeven.f64", "llvm.roundeven.f64", Float(64), "round", {Float(64)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate},

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

    // FCVTP/M. These only exist in armv8 and onwards, so we just skip them for
    // arm-32. LLVM doesn't seem to have intrinsics for them for SVE.
    {nullptr, "fcvtpu", UInt(32, 4), "fp_to_int_ceil", {Float(32, 4)}, ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveUnavailable},
    {nullptr, "fcvtmu", UInt(32, 4), "fp_to_int_floor", {Float(32, 4)}, ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveUnavailable},
    {nullptr, "fcvtps", Int(32, 4), "fp_to_int_ceil", {Float(32, 4)}, ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveUnavailable},
    {nullptr, "fcvtms", Int(32, 4), "fp_to_int_floor", {Float(32, 4)}, ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveUnavailable},
    {nullptr, "fcvtpu", UInt(32, 2), "fp_to_int_ceil", {Float(32, 2)}, ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveUnavailable},
    {nullptr, "fcvtmu", UInt(32, 2), "fp_to_int_floor", {Float(32, 2)}, ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveUnavailable},
    {nullptr, "fcvtps", Int(32, 2), "fp_to_int_ceil", {Float(32, 2)}, ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveUnavailable},
    {nullptr, "fcvtms", Int(32, 2), "fp_to_int_floor", {Float(32, 2)}, ArmIntrinsic::MangleRetArgs | ArmIntrinsic::SveUnavailable},

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
    {nullptr, "rshrn", Int(8, 8), "rounding_shift_right_narrow", {Int(16, 8), UInt(32)}},
    {nullptr, "rshrn", UInt(8, 8), "rounding_shift_right_narrow", {UInt(16, 8), UInt(32)}},
    {nullptr, "rshrn", Int(16, 4), "rounding_shift_right_narrow", {Int(32, 4), UInt(32)}},
    {nullptr, "rshrn", UInt(16, 4), "rounding_shift_right_narrow", {UInt(32, 4), UInt(32)}},
    {nullptr, "rshrn", Int(32, 2), "rounding_shift_right_narrow", {Int(64, 2), UInt(32)}},
    {nullptr, "rshrn", UInt(32, 2), "rounding_shift_right_narrow", {UInt(64, 2), UInt(32)}},

    // SHRN - Shift right narrow (by immediate in [1, output bits])
    // LLVM pattern matches these.

    // SQRSHL, UQRSHL - Saturating rounding shift left (by signed vector)
    {"vqrshifts", "sqrshl", Int(8, 8), "saturating_rounding_shift_left", {Int(8, 8), Int(8, 8)}, ArmIntrinsic::SveUnavailable},
    {"vqrshiftu", "uqrshl", UInt(8, 8), "saturating_rounding_shift_left", {UInt(8, 8), Int(8, 8)}, ArmIntrinsic::SveUnavailable},
    {"vqrshifts", "sqrshl", Int(16, 4), "saturating_rounding_shift_left", {Int(16, 4), Int(16, 4)}, ArmIntrinsic::SveUnavailable},
    {"vqrshiftu", "uqrshl", UInt(16, 4), "saturating_rounding_shift_left", {UInt(16, 4), Int(16, 4)}, ArmIntrinsic::SveUnavailable},
    {"vqrshifts", "sqrshl", Int(32, 2), "saturating_rounding_shift_left", {Int(32, 2), Int(32, 2)}, ArmIntrinsic::SveUnavailable},
    {"vqrshiftu", "uqrshl", UInt(32, 2), "saturating_rounding_shift_left", {UInt(32, 2), Int(32, 2)}, ArmIntrinsic::SveUnavailable},
    {"vqrshifts", "sqrshl", Int(64, 2), "saturating_rounding_shift_left", {Int(64, 2), Int(64, 2)}, ArmIntrinsic::SveUnavailable},
    {"vqrshiftu", "uqrshl", UInt(64, 2), "saturating_rounding_shift_left", {UInt(64, 2), Int(64, 2)}, ArmIntrinsic::SveUnavailable},

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

    // SPADAL, UPADAL - Pairwise add and accumulate long.
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
    // Mangle this one manually, there aren't that many and it is a special case.
    {nullptr, "sdot.v2i32.v8i8", Int(32, 2), "dot_product", {Int(32, 2), Int(8, 8), Int(8, 8)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveUnavailable},
    {nullptr, "udot.v2i32.v8i8", Int(32, 2), "dot_product", {Int(32, 2), UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveUnavailable},
    {nullptr, "udot.v2i32.v8i8", UInt(32, 2), "dot_product", {UInt(32, 2), UInt(8, 8), UInt(8, 8)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveUnavailable},
    {nullptr, "sdot.v4i32.v16i8", Int(32, 4), "dot_product", {Int(32, 4), Int(8, 16), Int(8, 16)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveUnavailable},
    {nullptr, "udot.v4i32.v16i8", Int(32, 4), "dot_product", {Int(32, 4), UInt(8, 16), UInt(8, 16)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveUnavailable},
    {nullptr, "udot.v4i32.v16i8", UInt(32, 4), "dot_product", {UInt(32, 4), UInt(8, 16), UInt(8, 16)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveUnavailable},
    // SVE versions.
    {nullptr, "sdot.nxv4i32", Int(32, 4), "dot_product", {Int(32, 4), Int(8, 16), Int(8, 16)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate | ArmIntrinsic::SveRequired},
    {nullptr, "udot.nxv4i32", Int(32, 4), "dot_product", {Int(32, 4), UInt(8, 16), UInt(8, 16)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate | ArmIntrinsic::SveRequired},
    {nullptr, "udot.nxv4i32", UInt(32, 4), "dot_product", {UInt(32, 4), UInt(8, 16), UInt(8, 16)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate | ArmIntrinsic::SveRequired},
    {nullptr, "sdot.nxv2i64", Int(64, 2), "dot_product", {Int(64, 2), Int(16, 8), Int(16, 8)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate | ArmIntrinsic::Neon64Unavailable | ArmIntrinsic::SveRequired},
    {nullptr, "udot.nxv2i64", Int(64, 2), "dot_product", {Int(64, 2), UInt(16, 8), UInt(16, 8)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate | ArmIntrinsic::Neon64Unavailable | ArmIntrinsic::SveRequired},
    {nullptr, "udot.nxv2i64", UInt(64, 2), "dot_product", {UInt(64, 2), UInt(16, 8), UInt(16, 8)}, ArmIntrinsic::NoMangle | ArmIntrinsic::SveNoPredicate | ArmIntrinsic::Neon64Unavailable | ArmIntrinsic::SveRequired},

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

llvm::Type *CodeGen_ARM::llvm_type_with_constraint(const Type &t, bool scalars_are_vectors,
                                                   VectorTypeConstraint constraint) {
    llvm::Type *ret = llvm_type_of(t.element_of());
    if (!t.is_scalar() || scalars_are_vectors) {
        int lanes = t.lanes();
        if (constraint == VectorTypeConstraint::VScale) {
            lanes /= target_vscale();
        }
        ret = get_vector_type(ret, lanes, constraint);
    }
    return ret;
}

llvm::Function *CodeGen_ARM::define_intrin_wrapper(const std::string &inner_name,
                                                   const Type &ret_type,
                                                   const std::string &mangled_name,
                                                   const std::vector<Type> &arg_types,
                                                   int intrinsic_flags,
                                                   bool sve_intrinsic) {

    auto to_llvm_type = [&](const Type &t) {
        return llvm_type_with_constraint(t, (intrinsic_flags & ArmIntrinsic::ScalarsAreVectors),
                                         !sve_intrinsic ? VectorTypeConstraint::Fixed : VectorTypeConstraint::VScale);
    };

    llvm::Type *llvm_ret_type = to_llvm_type(ret_type);
    std::vector<llvm::Type *> llvm_arg_types;
    std::transform(arg_types.begin(), arg_types.end(), std::back_inserter(llvm_arg_types), to_llvm_type);

    const bool add_predicate = sve_intrinsic && !(intrinsic_flags & ArmIntrinsic::SveNoPredicate);
    bool add_inactive_arg = sve_intrinsic && (intrinsic_flags & ArmIntrinsic::SveInactiveArg);
    bool split_arg0 = intrinsic_flags & ArmIntrinsic::SplitArg0;

    if (!(add_inactive_arg || add_predicate || split_arg0)) {
        // No need to wrap
        return get_llvm_intrin(llvm_ret_type, mangled_name, llvm_arg_types);
    }

    std::vector<llvm::Type *> inner_llvm_arg_types;
    std::vector<Value *> inner_args;
    internal_assert(!arg_types.empty());
    const int inner_lanes = split_arg0 ? arg_types[0].lanes() / 2 : arg_types[0].lanes();

    if (add_inactive_arg) {
        // The fallback value has the same type as ret value.
        // We don't use this, so just pad it with 0.
        inner_llvm_arg_types.push_back(llvm_ret_type);

        Value *zero = Constant::getNullValue(llvm_ret_type);
        inner_args.push_back(zero);
    }
    if (add_predicate) {
        llvm::Type *pred_type = to_llvm_type(Int(1, inner_lanes));
        inner_llvm_arg_types.push_back(pred_type);
        // Halide does not have general support for predication so use
        // constant true for all lanes.
        Value *ptrue = Constant::getAllOnesValue(pred_type);
        inner_args.push_back(ptrue);
    }
    if (split_arg0) {
        llvm::Type *split_arg_type = to_llvm_type(arg_types[0].with_lanes(inner_lanes));
        inner_llvm_arg_types.push_back(split_arg_type);
        inner_llvm_arg_types.push_back(split_arg_type);
        internal_assert(arg_types.size() == 1);
    } else {
        // Push back all argument typs which Halide defines
        std::copy(llvm_arg_types.begin(), llvm_arg_types.end(), std::back_inserter(inner_llvm_arg_types));
    }

    llvm::Function *inner = get_llvm_intrin(llvm_ret_type, mangled_name, inner_llvm_arg_types);
    llvm::FunctionType *inner_ty = inner->getFunctionType();

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
        internal_assert(inner_llvm_arg_types.size() == 2);
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

void CodeGen_ARM::init_module() {
    CodeGen_Posix::init_module();

    const bool has_neon = !target.has_feature(Target::NoNEON);
    const bool has_sve = target.has_feature(Target::SVE2);
    if (!(has_neon || has_sve)) {
        return;
    }

    enum class SIMDFlavors {
        NeonWidthX1,
        NeonWidthX2,
        SVE,
    };

    std::vector<SIMDFlavors> flavors;
    if (has_neon) {
        flavors.push_back(SIMDFlavors::NeonWidthX1);
        flavors.push_back(SIMDFlavors::NeonWidthX2);
    }
    if (has_sve) {
        flavors.push_back(SIMDFlavors::SVE);
    }

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

        // This makes up to three passes defining intrinsics for 64-bit,
        // 128-bit, and, if SVE is avaailable, whatever the SVE target width
        // is. Some variants will not result in a definition getting added based
        // on the target and the intrinsic flags. The intrinsic width may be
        // scaled and one of two opcodes may be selected by different
        // interations of this loop.
        for (const auto flavor : flavors) {
            const bool is_sve = (flavor == SIMDFlavors::SVE);

            // Skip intrinsics that are NEON or SVE only depending on whether compiling for SVE.
            if (is_sve) {
                if (intrin.flags & ArmIntrinsic::SveUnavailable) {
                    continue;
                }
            } else {
                if (intrin.flags & ArmIntrinsic::SveRequired) {
                    continue;
                }
            }
            if ((target.bits == 64) &&
                (intrin.flags & ArmIntrinsic::Neon64Unavailable) &&
                !is_sve) {
                continue;
            }
            // Already declared in the x1 pass.
            if ((flavor == SIMDFlavors::NeonWidthX2) &&
                !(intrin.flags & ArmIntrinsic::HalfWidth)) {
                continue;
            }

            string full_name = intrin_name;
            const bool is_vanilla_intrinsic = starts_with(full_name, "llvm.");
            if (!is_vanilla_intrinsic && (intrin.flags & ArmIntrinsic::NoPrefix) == 0) {
                if (target.bits == 32) {
                    full_name = "llvm.arm.neon." + full_name;
                } else {
                    full_name = (is_sve ? "llvm.aarch64.sve." : "llvm.aarch64.neon.") + full_name;
                }
            }

            int width_factor = 1;
            if (!((intrin.ret_type.lanes <= 1) && (intrin.flags & ArmIntrinsic::NoMangle))) {
                switch (flavor) {
                case SIMDFlavors::NeonWidthX1:
                    width_factor = 1;
                    break;
                case SIMDFlavors::NeonWidthX2:
                    width_factor = 2;
                    break;
                case SIMDFlavors::SVE:
                    width_factor = (intrin.flags & ArmIntrinsic::HalfWidth) ? 2 : 1;
                    width_factor *= target_vscale();
                    break;
                }
            }

            Type ret_type = intrin.ret_type;
            ret_type = ret_type.with_lanes(ret_type.lanes() * width_factor);
            internal_assert(ret_type.bits() * ret_type.lanes() <= 128 * width_factor) << full_name << "\n";
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
                } else {
                    types = {ret_type};
                }
                for (const Type &t : types) {
                    std::string llvm_vector_prefix = is_sve ? ".nxv" : ".v";
                    int mangle_lanes = t.lanes() / (is_sve ? target_vscale() : 1);
                    mangled_name_builder << llvm_vector_prefix << mangle_lanes;
                    if (t.is_int() || t.is_uint()) {
                        mangled_name_builder << "i";
                    } else if (t.is_float()) {
                        mangled_name_builder << "f";
                    }
                    mangled_name_builder << t.bits();
                }
            }
            string mangled_name = mangled_name_builder.str();

            llvm::Function *intrin_impl = define_intrin_wrapper(
                intrin.name, ret_type, mangled_name, arg_types,
                intrin.flags, is_sve);

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

void CodeGen_ARM::compile_func(const LoweredFunc &f,
                               const string &simple_name,
                               const string &extern_name) {

    LoweredFunc func = f;

    if (target.os != Target::IOS && target.os != Target::OSX) {
        // Substitute in strided loads to get vld2/3/4 emission. We don't do it
        // on Apple silicon, because doing a dense load and then shuffling is
        // actually faster.
        func.body = SubstituteInStridedLoads().mutate(func.body);
    }
    // Look for opportunities to turn a + (b << c) into umlal/smlal
    // and a - (b << c) into umlsl/smlsl.
    func.body = distribute_shifts(func.body, /* multiply_adds */ true);

    CodeGen_Posix::compile_func(func, simple_name, extern_name);
}

void CodeGen_ARM::begin_func(LinkageType linkage, const std::string &simple_name,
                             const std::string &extern_name, const std::vector<LoweredArgument> &args) {
    CodeGen_Posix::begin_func(linkage, simple_name, extern_name, args);

    // TODO(https://github.com/halide/Halide/issues/8092): There is likely a
    // better way to ensure this is only generated for the outermost function
    // that is being compiled. Avoiding the assert on inner functions is both an
    // efficiency and a correctness issue as the assertion code may not compile
    // in all contexts.
    if (linkage != LinkageType::Internal) {
        int effective_vscale = target_vscale();
        if (effective_vscale != 0 && !target.has_feature(Target::NoAsserts)) {
            // Make sure run-time vscale is equal to compile-time vscale
            Expr runtime_vscale = Call::make(Int(32), Call::get_runtime_vscale, {}, Call::PureIntrinsic);
            Value *val_runtime_vscale = codegen(runtime_vscale);
            Value *val_compiletime_vscale = ConstantInt::get(i32_t, effective_vscale);
            Value *cond = builder->CreateICmpEQ(val_runtime_vscale, val_compiletime_vscale);
            create_assertion(cond, Call::make(Int(32), "halide_error_vscale_invalid",
                                              {simple_name, runtime_vscale, Expr(effective_vscale)}, Call::Extern));
        }
    }
}

void CodeGen_ARM::visit(const Cast *op) {
    if (!simd_intrinsics_disabled() && op->type.is_vector()) {
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

    // LLVM fptoui generates fcvtzs or fcvtzu in inconsistent way
    if (op->value.type().is_float() && op->type.is_int_or_uint()) {
        if (Value *v = call_overloaded_intrin(op->type, "fp_to_int", {op->value})) {
            value = v;
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Add *op) {
    if (simd_intrinsics_disabled() ||
        !op->type.is_vector() ||
        !target.has_feature(Target::ARMDotProd) ||
        !op->type.is_int_or_uint() ||
        op->type.bits() != 32) {
        CodeGen_Posix::visit(op);
        return;
    }

    struct Pattern {
        Expr pattern;
        const char *intrin;
        Type coeff_type = UInt(8);
    };

    // Initial values.
    Expr init_i32 = Variable::make(Int(32, 0), "init");
    Expr init_u32 = Variable::make(UInt(32, 0), "init");
    // Values
    Expr a_i8 = Variable::make(Int(8, 0), "a"), b_i8 = Variable::make(Int(8, 0), "b");
    Expr c_i8 = Variable::make(Int(8, 0), "c"), d_i8 = Variable::make(Int(8, 0), "d");
    Expr a_u8 = Variable::make(UInt(8, 0), "a"), b_u8 = Variable::make(UInt(8, 0), "b");
    Expr c_u8 = Variable::make(UInt(8, 0), "c"), d_u8 = Variable::make(UInt(8, 0), "d");
    // Coefficients
    Expr ac_i8 = Variable::make(Int(8, 0), "ac"), bc_i8 = Variable::make(Int(8, 0), "bc");
    Expr cc_i8 = Variable::make(Int(8, 0), "cc"), dc_i8 = Variable::make(Int(8, 0), "dc");
    Expr ac_u8 = Variable::make(UInt(8, 0), "ac"), bc_u8 = Variable::make(UInt(8, 0), "bc");
    Expr cc_u8 = Variable::make(UInt(8, 0), "cc"), dc_u8 = Variable::make(UInt(8, 0), "dc");

    Expr ma_i8 = widening_mul(a_i8, ac_i8);
    Expr mb_i8 = widening_mul(b_i8, bc_i8);
    Expr mc_i8 = widening_mul(c_i8, cc_i8);
    Expr md_i8 = widening_mul(d_i8, dc_i8);

    Expr ma_u8 = widening_mul(a_u8, ac_u8);
    Expr mb_u8 = widening_mul(b_u8, bc_u8);
    Expr mc_u8 = widening_mul(c_u8, cc_u8);
    Expr md_u8 = widening_mul(d_u8, dc_u8);

    static const Pattern patterns[] = {
        // Signed variants.
        {(init_i32 + widening_add(ma_i8, mb_i8)) + widening_add(mc_i8, md_i8), "dot_product"},
        {init_i32 + (widening_add(ma_i8, mb_i8) + widening_add(mc_i8, md_i8)), "dot_product"},
        {widening_add(ma_i8, mb_i8) + widening_add(mc_i8, md_i8), "dot_product"},

        // Unsigned variants.
        {(init_u32 + widening_add(ma_u8, mb_u8)) + widening_add(mc_u8, md_u8), "dot_product"},
        {init_u32 + (widening_add(ma_u8, mb_u8) + widening_add(mc_u8, md_u8)), "dot_product"},
        {widening_add(ma_u8, mb_u8) + widening_add(mc_u8, md_u8), "dot_product"},
    };

    std::map<std::string, Expr> matches;
    for (const Pattern &p : patterns) {
        if (expr_match(p.pattern, op, matches)) {
            Expr init;
            auto it = matches.find("init");
            if (it == matches.end()) {
                init = make_zero(op->type);
            } else {
                init = it->second;
            }
            Expr values = Shuffle::make_interleave({matches["a"], matches["b"],
                                                    matches["c"], matches["d"]});
            Expr coeffs = Shuffle::make_interleave({matches["ac"], matches["bc"],
                                                    matches["cc"], matches["dc"]});
            value = call_overloaded_intrin(op->type, p.intrin, {init, values, coeffs});
            if (value) {
                return;
            }
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Sub *op) {
    if (simd_intrinsics_disabled()) {
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

    // Peep-hole (0 - b) pattern to generate "negate" instruction
    if (is_const_zero(op->a)) {
        if (target_vscale() != 0) {
            if ((op->type.bits() >= 8 && op->type.is_int())) {
                if (Value *v = call_overloaded_intrin(op->type, "negate", {op->b})) {
                    value = v;
                    return;
                }
            } else if (op->type.bits() >= 16 && op->type.is_float()) {
                value = builder->CreateFNeg(codegen(op->b));
                return;
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
                    a = get_splat(op->type.lanes(), a);
                }
                value = builder->CreateFSub(a, b);
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
    if (!simd_intrinsics_disabled() && (op->type.is_float() || op->type.is_vector())) {
        value = call_overloaded_intrin(op->type, "min", {op->a, op->b});
        if (value) {
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Max *op) {
    // Use a 2-wide vector for scalar floats.
    if (!simd_intrinsics_disabled() && (op->type.is_float() || op->type.is_vector())) {
        value = call_overloaded_intrin(op->type, "max", {op->a, op->b});
        if (value) {
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

    if (simd_intrinsics_disabled()) {
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
            // TODO(zvookin): Handle vector_bits_*.
            if (vec_bits % 128 == 0) {
                type_ok_for_vst = true;
                int target_vector_bits = target.vector_bits;
                if (target_vector_bits == 0) {
                    target_vector_bits = 128;
                }
                intrin_type = intrin_type.with_lanes(target_vector_bits / t.bits());
            } else if (vec_bits % 64 == 0) {
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

        bool is_sve = target.has_feature(Target::SVE2);

        // Declare the function
        std::ostringstream instr;
        vector<llvm::Type *> arg_types;
        llvm::Type *intrin_llvm_type = llvm_type_with_constraint(intrin_type, false, is_sve ? VectorTypeConstraint::VScale : VectorTypeConstraint::Fixed);
        if (target.bits == 32) {
            instr << "llvm.arm.neon.vst"
                  << num_vecs
                  << ".p0"
                  << ".v"
                  << intrin_type.lanes()
                  << (t.is_float() ? 'f' : 'i')
                  << t.bits();
            arg_types = vector<llvm::Type *>(num_vecs + 2, intrin_llvm_type);
            arg_types.front() = i8_t->getPointerTo();
            arg_types.back() = i32_t;
        } else {
            if (is_sve) {
                instr << "llvm.aarch64.sve.st"
                      << num_vecs
                      << ".nxv"
                      << (intrin_type.lanes() / target_vscale())
                      << (t.is_float() ? 'f' : 'i')
                      << t.bits();
                arg_types = vector<llvm::Type *>(num_vecs, intrin_llvm_type);
                arg_types.emplace_back(get_vector_type(i1_t, intrin_type.lanes() / target_vscale(), VectorTypeConstraint::VScale));  // predicate
                arg_types.emplace_back(llvm_type_of(intrin_type.element_of())->getPointerTo());
            } else {
                instr << "llvm.aarch64.neon.st"
                      << num_vecs
                      << ".v"
                      << intrin_type.lanes()
                      << (t.is_float() ? 'f' : 'i')
                      << t.bits()
                      << ".p0";
                arg_types = vector<llvm::Type *>(num_vecs + 1, intrin_llvm_type);
                arg_types.back() = llvm_type_of(intrin_type.element_of())->getPointerTo();
            }
        }
        llvm::FunctionType *fn_type = FunctionType::get(llvm::Type::getVoidTy(*context), arg_types, false);
        llvm::FunctionCallee fn = module->getOrInsertFunction(instr.str(), fn_type);
        internal_assert(fn);

        // SVE2 supports predication for smaller than whole vector size.
        internal_assert(target.has_feature(Target::SVE2) || (t.lanes() >= intrin_type.lanes()));

        for (int i = 0; i < t.lanes(); i += intrin_type.lanes()) {
            Expr slice_base = simplify(ramp->base + i * num_vecs);
            Expr slice_ramp = Ramp::make(slice_base, ramp->stride, intrin_type.lanes() * num_vecs);
            Value *ptr = codegen_buffer_pointer(op->name, shuffle->vectors[0].type().element_of(), slice_base);

            vector<Value *> slice_args = args;
            // Take a slice of each arg
            for (int j = 0; j < num_vecs; j++) {
                slice_args[j] = slice_vector(slice_args[j], i, intrin_type.lanes());
                slice_args[j] = convert_fixed_or_scalable_vector_type(slice_args[j], get_vector_type(slice_args[j]->getType()->getScalarType(), intrin_type.lanes()));
            }

            if (target.bits == 32) {
                // The arm32 versions take an i8*, regardless of the type stored.
                ptr = builder->CreatePointerCast(ptr, i8_t->getPointerTo());
                // Set the pointer argument
                slice_args.insert(slice_args.begin(), ptr);
                // Set the alignment argument
                slice_args.push_back(ConstantInt::get(i32_t, alignment));
            } else {
                if (is_sve) {
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

            if (is_sve) {
                for (auto &arg : slice_args) {
                    if (arg->getType()->isVectorTy()) {
                        arg = match_vector_type_scalable(arg, VectorTypeConstraint::VScale);
                    }
                }
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
            const int vscale_natural_lanes = natural_lanes / target_vscale();

            Expr base = 0;
            Value *elt_ptr = codegen_buffer_pointer(op->name, elt, base);
            Value *val = codegen(op->value);
            Value *index = codegen(op->index);
            Value *store_pred_val = codegen(op->predicate);

            llvm::Type *slice_type = get_vector_type(llvm_type_of(elt), vscale_natural_lanes, VectorTypeConstraint::VScale);
            llvm::Type *slice_index_type = get_vector_type(llvm_type_of(op->index.type().element_of()), vscale_natural_lanes, VectorTypeConstraint::VScale);
            llvm::Type *pred_type = get_vector_type(llvm_type_of(op->predicate.type().element_of()), vscale_natural_lanes, VectorTypeConstraint::VScale);

            std::ostringstream instr;
            instr << "llvm.aarch64.sve.st1.scatter.uxtw."
                  << (elt.bits() != 8 ? "index." : "")  // index is scaled into bytes
                  << "nxv"
                  << vscale_natural_lanes
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
                vpred_val = convert_fixed_or_scalable_vector_type(vpred_val, pred_type);
                if (is_predicated_store) {
                    Value *sliced_store_vpred_val = slice_vector(store_pred_val, i, natural_lanes);
                    vpred_val = builder->CreateAnd(vpred_val, sliced_store_vpred_val);
                }

                slice_value = match_vector_type_scalable(slice_value, VectorTypeConstraint::VScale);
                vpred_val = match_vector_type_scalable(vpred_val, VectorTypeConstraint::VScale);
                slice_index = match_vector_type_scalable(slice_index, VectorTypeConstraint::VScale);
                CallInst *store = builder->CreateCall(fn, {slice_value, vpred_val, elt_ptr, slice_index});
                add_tbaa_metadata(store, op->name, op->index);
            }

            return;
        }
    }

    // If the stride is one or minus one, we can deal with that using vanilla codegen
    const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : nullptr;
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
    const bool is_predicated_load = !is_const_one(op->predicate);
    if (is_predicated_load && !target.has_feature(Target::SVE2)) {
        CodeGen_Posix::visit(op);
        return;
    }

    if (simd_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    const Ramp *ramp = op->index.as<Ramp>();

    // We only deal with ramps here
    if (!ramp && !target.has_feature(Target::SVE2)) {
        CodeGen_Posix::visit(op);
        return;
    }

    // If the stride is in [-1, 1], we can deal with that using vanilla codegen
    const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : nullptr;
    if (stride && (-1 <= stride->value && stride->value <= 1) &&
        !target.has_feature(Target::SVE2)) {
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
                vpred_val = convert_fixed_or_scalable_vector_type(vpred_val, get_vector_type(vpred_val->getType()->getScalarType(), slice_lanes));
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
            const int vscale_natural_lanes = natural_lanes / target_vscale();

            Expr base = 0;
            Value *elt_ptr = codegen_buffer_pointer(op->name, elt, base);
            Value *index = codegen(op->index);
            Value *load_pred_val = codegen(op->predicate);

            llvm::Type *slice_type = get_vector_type(llvm_type_of(elt), vscale_natural_lanes, VectorTypeConstraint::VScale);
            llvm::Type *slice_index_type = get_vector_type(llvm_type_of(op->index.type().element_of()), vscale_natural_lanes, VectorTypeConstraint::VScale);
            llvm::Type *pred_type = get_vector_type(llvm_type_of(op->predicate.type().element_of()), vscale_natural_lanes, VectorTypeConstraint::VScale);

            std::ostringstream instr;
            instr << "llvm.aarch64.sve.ld1.gather.uxtw."
                  << (elt.bits() != 8 ? "index." : "")  // index is scaled into bytes
                  << "nxv"
                  << vscale_natural_lanes
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

                vpred_val = match_vector_type_scalable(vpred_val, VectorTypeConstraint::VScale);
                slice_index = match_vector_type_scalable(slice_index, VectorTypeConstraint::VScale);
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

void CodeGen_ARM::visit(const Shuffle *op) {
    // For small strided loads on non-Apple hardware, we may want to use vld2,
    // vld3, vld4, etc. These show up in the IR as slice shuffles of wide dense
    // loads. LLVM expects the same. The base codegen class breaks the loads
    // into native vectors, which triggers shuffle instructions rather than
    // vld2, vld3, vld4. So here we explicitly do the load as a single big dense
    // load.
    int stride = op->slice_stride();
    const Load *load = op->vectors[0].as<Load>();
    if (target.os != Target::IOS && target.os != Target::OSX &&
        load &&
        op->vectors.size() == 1 &&
        2 <= stride && stride <= 4 &&
        op->slice_begin() < stride &&
        load->type.lanes() == stride * op->type.lanes()) {

        value = codegen_dense_vector_load(load, nullptr, /* slice_to_native */ false);
        value = shuffle_vectors(value, op->indices);
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_ARM::visit(const Ramp *op) {
    if (target_vscale() != 0 && op->type.is_int_or_uint()) {
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
        } else {
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
    if (simd_intrinsics_disabled()) {
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
        {VectorReduce::Add, 4, i32(widening_mul(wild_i8x_, wild_i8x_)), "dot_product", Target::SVE2},
        {VectorReduce::Add, 4, i32(widening_mul(wild_u8x_, wild_u8x_)), "dot_product", Target::SVE2},
        {VectorReduce::Add, 4, u32(widening_mul(wild_u8x_, wild_u8x_)), "dot_product", Target::SVE2},
        {VectorReduce::Add, 4, i64(widening_mul(wild_i16x_, wild_i16x_)), "dot_product", Target::SVE2},
        {VectorReduce::Add, 4, i64(widening_mul(wild_u16x_, wild_u16x_)), "dot_product", Target::SVE2},
        {VectorReduce::Add, 4, u64(widening_mul(wild_u16x_, wild_u16x_)), "dot_product", Target::SVE2},
        // A sum is the same as a dot product with a vector of ones, and this appears to
        // be a bit faster.
        {VectorReduce::Add, 4, i32(wild_i8x_), "dot_product", Target::ARMDotProd, {1}},
        {VectorReduce::Add, 4, i32(wild_u8x_), "dot_product", Target::ARMDotProd, {1}},
        {VectorReduce::Add, 4, u32(wild_u8x_), "dot_product", Target::ARMDotProd, {1}},
        {VectorReduce::Add, 4, i32(wild_i8x_), "dot_product", Target::SVE2, {1}},
        {VectorReduce::Add, 4, i32(wild_u8x_), "dot_product", Target::SVE2, {1}},
        {VectorReduce::Add, 4, u32(wild_u8x_), "dot_product", Target::SVE2, {1}},
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
                // TODO: this could be written as a pattern with widen_right_add (#6951).
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
    if (target_vscale() == 0) {
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
             << ".nxv" << (native_lanes / target_vscale()) << (op->type.is_float() ? "f" : "i") << input_bits;

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
                arg_types.push_back(llvm_type_with_constraint(e.type(), false, VectorTypeConstraint::VScale));
            }
            FunctionType *func_t = FunctionType::get(llvm_type_with_constraint(intrin_ret_type, false, VectorTypeConstraint::VScale),
                                                     arg_types, false);
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
            return "apple-a7";
        } else if (target.os == Target::OSX) {
            return "apple-m1";
        } else if (target.has_feature(Target::SVE2)) {
            return "cortex-x1";
        } else {
            return "generic";
        }
    }
}

string CodeGen_ARM::mcpu_tune() const {
    return mcpu_target();
}

string CodeGen_ARM::mattrs() const {
    std::vector<std::string_view> attrs;
    if (target.has_feature(Target::ARMFp16)) {
        attrs.emplace_back("+fullfp16");
    }
    if (target.has_feature(Target::ARMv8a)) {
        attrs.emplace_back("+v8a");
    }
    if (target.has_feature(Target::ARMv81a)) {
        attrs.emplace_back("+v8.1a");
    }
    if (target.has_feature(Target::ARMv82a)) {
        attrs.emplace_back("+v8.2a");
    }
    if (target.has_feature(Target::ARMv83a)) {
        attrs.emplace_back("+v8.3a");
    }
    if (target.has_feature(Target::ARMv84a)) {
        attrs.emplace_back("+v8.4a");
    }
    if (target.has_feature(Target::ARMv85a)) {
        attrs.emplace_back("+v8.5a");
    }
    if (target.has_feature(Target::ARMv86a)) {
        attrs.emplace_back("+v8.6a");
    }
    if (target.has_feature(Target::ARMv87a)) {
        attrs.emplace_back("+v8.7a");
    }
    if (target.has_feature(Target::ARMv88a)) {
        attrs.emplace_back("+v8.8a");
    }
    if (target.has_feature(Target::ARMv89a)) {
        attrs.emplace_back("+v8.9a");
    }
    if (target.has_feature(Target::ARMDotProd)) {
        attrs.emplace_back("+dotprod");
    }
    if (target.bits == 32) {
        if (target.has_feature(Target::ARMv7s)) {
            attrs.emplace_back("+neon");
        }
        if (!target.has_feature(Target::NoNEON)) {
            attrs.emplace_back("+neon");
        } else {
            attrs.emplace_back("-neon");
        }
    } else {
        // TODO: Should Halide's SVE flags be 64-bit only?
        // TODO: Should we add "-neon" if NoNEON is set? Does this make any sense?
        if (target.has_feature(Target::SVE2)) {
            attrs.emplace_back("+sve2");
        } else if (target.has_feature(Target::SVE)) {
            attrs.emplace_back("+sve");
        }
        if (target.os == Target::IOS || target.os == Target::OSX) {
            attrs.emplace_back("+reserve-x18");
        }
    }
    return join_strings(attrs, ",");
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
    if (target.has_feature(Target::SVE) || target.has_feature(Target::SVE2)) {
        return std::max(target.vector_bits, 128);
    } else {
        return 128;
    }
}

int CodeGen_ARM::target_vscale() const {
    if (target.features_any_of({Target::SVE, Target::SVE2})) {
        user_assert(target.vector_bits != 0) << "For SVE/SVE2 support, target_vector_bits=<size> must be set in target.\n";
        user_assert((target.vector_bits % 128) == 0) << "For SVE/SVE2 support, target_vector_bits must be a multiple of 128.\n";
        return target.vector_bits / 128;
    }

    return 0;
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
