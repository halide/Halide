#include "CSE.h"
#include "CodeGen_Internal.h"
#include "CodeGen_Posix.h"
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

using std::string;
using std::vector;

#if defined(WITH_RISCV)

namespace {

constexpr int max_intrinsic_args = 4;

struct IntrinsicArgPattern {
    Type type;
    int relative_scale;

    IntrinsicArgPattern(const Type &type)
        : type(type), relative_scale(1) {
    }
    IntrinsicArgPattern(halide_type_code_t code)
        : type(code, 8, 1), relative_scale(1) {
    }
    IntrinsicArgPattern(halide_type_code_t code, int relative_scale)
        : type(code, 8, 1), relative_scale(relative_scale) {
    }
    IntrinsicArgPattern()
        : type(), relative_scale(0) {
    }
};

enum RVVIntrinsicFlags {
    AddVLArg = 1 << 0,          // Add a constant full size vector length argument
    RoundDown = 1 << 1,         // Set vxrm rounding mode to down (rdn) before intrinsic.
    RoundUp = 1 << 2,           // Set vxrm rounding mode to up (rdu) before intrinsic.
    MangleReturnType = 1 << 3,  // Put return type mangling at start of type list.
    ReverseBinOp = 1 << 4,      // Switch first two arguments to handle asymmetric ops.
    Commutes = 1 << 5,          // Indicates first two arguments can flip, for vector/scalar ops.
};

struct RISCVIntrinsic {
    const char *riscv_name;
    IntrinsicArgPattern ret_type;
    const char *name;
    IntrinsicArgPattern arg_types[max_intrinsic_args];
    int flags;
};

// TODO: Consider moving enum out to global scope to eliminate "RISCVIntrinsic::"
const RISCVIntrinsic signed_intrinsic_defs[] = {
    {"vaadd", Type::Int, "halving_add", {Type::Int, Type::Int}, AddVLArg | RoundDown | Commutes},
    {"vaadd", Type::Int, "rounding_halving_add", {Type::Int, Type::Int}, AddVLArg | RoundUp | Commutes},
    {"vwadd", {Type::Int, 2}, "widening_add", {Type::Int, Type::Int}, AddVLArg | MangleReturnType | Commutes},
    {"vwsub", {Type::Int, 2}, "widening_sub", {Type::Int, Type::Int}, AddVLArg | MangleReturnType},
    {"vwmul", {Type::Int, 2}, "widening_mul", {Type::Int, Type::Int}, AddVLArg | MangleReturnType | Commutes},
};

const RISCVIntrinsic unsigned_intrinsic_defs[] = {
    {"vaaddu", {Type::UInt}, "halving_add", {Type::UInt, Type::UInt}, AddVLArg | RoundDown | Commutes},
    {"vaaddu", {Type::UInt}, "rounding_halving_add", {Type::UInt, Type::UInt}, AddVLArg | RoundUp | Commutes},
    {"vwaddu", {Type::UInt, 2}, "widening_add", {Type::UInt, Type::UInt}, AddVLArg | MangleReturnType | Commutes},
    {"vwsubu", {Type::UInt, 2}, "widening_sub", {Type::UInt, Type::UInt}, AddVLArg | MangleReturnType},
    {"vwmulu", {Type::UInt, 2}, "widening_mul", {Type::UInt, Type::UInt}, AddVLArg | MangleReturnType | Commutes},
};

const RISCVIntrinsic mixed_sign_intrinsic_defs[] = {
    {"vwmulsu", {Type::Int, 2}, "widening_mul", {Type::Int, Type::UInt}, AddVLArg | MangleReturnType},
    {"vwmulsu", {Type::Int, 2}, "widening_mul", {Type::UInt, Type::Int}, AddVLArg | MangleReturnType | ReverseBinOp},
};

const RISCVIntrinsic *MatchRISCVIntrisic(const Call *op) {
    if (op->is_intrinsic() && op->args.size() == 2 &&
        op->type.is_int_or_uint() &&
        op->args[0].type().is_int_or_uint() &&
        op->args[1].type().is_int_or_uint()) {
        if (op->args[0].type().is_int() &&
            op->args[1].type().is_int()) {
            for (auto const &i : signed_intrinsic_defs) {
                if (i.name == op->name) {
                    return &i;
                }
            }
        } else if (op->args[0].type().is_uint() &&
                   op->args[1].type().is_uint()) {
            for (auto const &i : unsigned_intrinsic_defs) {
                if (i.name == op->name) {
                    return &i;
                }
            }
        } else {
            for (auto const &i : mixed_sign_intrinsic_defs) {
                if (i.name == op->name &&
                    op->args[0].type().code() != i.arg_types[0].type.code()) {
                    return &i;
                }
            }
        }
    }

    return nullptr;
}

/** A code generator that emits RISC-V code from a given Halide stmt. */
class CodeGen_RISCV : public CodeGen_Posix {
public:
    /** Create a RISC-V code generator. Processor features can be
     * enabled using the appropriate flags in the target struct. */
    CodeGen_RISCV(const Target &);

protected:
    using CodeGen_Posix::visit;

    string mcpu_target() const override;
    string mcpu_tune() const override;
    string mattrs() const override;
    string mabi() const override;
    bool use_soft_float_abi() const override;
    int native_vector_bits() const override;
    int maximum_vector_bits() const override;
    int target_vscale() const override;

    void visit(const Call *) override;

private:
    bool call_riscv_vector_intrinsic(const RISCVIntrinsic &intrin, const Call *op);
};

CodeGen_RISCV::CodeGen_RISCV(const Target &t)
    : CodeGen_Posix(t) {
    use_llvm_vp_intrinsics = true;
    user_assert(native_vector_bits() > 0) << "No vector_bits was specified for RISCV codegen; "
                                          << "this is almost certainly a mistake. You should add -rvv-vector_bits_N "
                                          << "to your Target string, where N is the SIMD width in bits (e.g. 128).";
#if LLVM_VERSION < 170
    user_warning << "RISCV codegen is only tested with LLVM 17.0 or later; it is unlikely to work well with earlier versions of LLVM.\n";
#endif
}

string CodeGen_RISCV::mcpu_target() const {
    return "";
}

string CodeGen_RISCV::mcpu_tune() const {
    return mcpu_target();
}

string CodeGen_RISCV::mattrs() const {
    // Note: the default march is "rv[32|64]imafdc",
    // which includes standard extensions:
    //   +m Integer Multiplication and Division,
    //   +a Atomic Instructions,
    //   +f Single-Precision Floating-Point,
    //   +d Double-Precision Floating-Point,
    //   +c Compressed Instructions,
    string arch_flags = "+m,+a,+f,+d,+c";

    if (target.has_feature(Target::RVV)) {
        arch_flags += ",+v";
#if LLVM_VERSION >= 160
        if (target.vector_bits != 0) {
            arch_flags += ",+zvl" + std::to_string(target.vector_bits) + "b";
        }
#endif
    }
    return arch_flags;
}

string CodeGen_RISCV::mabi() const {
    string abi;
    if (target.bits == 32) {
        abi = "ilp32";
    } else {
        abi = "lp64";
    }
    if (!target.has_feature(Target::SoftFloatABI)) {
        abi += "d";
    }
    return abi;
}

bool CodeGen_RISCV::use_soft_float_abi() const {
    return target.has_feature(Target::SoftFloatABI);
}

int CodeGen_RISCV::native_vector_bits() const {
    if (target.vector_bits != 0 &&
        target.has_feature(Target::RVV)) {
        return target.vector_bits;
    }
    return 0;
}

int CodeGen_RISCV::maximum_vector_bits() const {
    return native_vector_bits() * 8;
}

int CodeGen_RISCV::target_vscale() const {
    if (target.vector_bits != 0 &&
        target.has_feature(Target::RVV)) {
        internal_assert((target.vector_bits % 64) == 0);
        return target.vector_bits / 64;
    }

    return 0;
}

void CodeGen_RISCV::visit(const Call *op) {
    const RISCVIntrinsic *intrinsic_def = MatchRISCVIntrisic(op);

    bool handled = (intrinsic_def != nullptr) &&
                   call_riscv_vector_intrinsic(*intrinsic_def, op);
    if (!handled) {
        CodeGen_Posix::visit(op);
    }
}

// For vscale types, the lane count must be rounded up to a multiple of vscale.
int vscale_lanes(int vscale, const Type &type) {
    return ((type.lanes() + vscale - 1) / vscale) * vscale;
}

/* Currently this assumes the default pattern for RISC V intrinsics:
 *     - All widths of signed/unsigned/floating-point are supported.
 *     - All LMUL values are supported.
 *     - There is a vector/scalar version in which the second argument is a scalar.
 *       The Commutative flag is used to decide whether to automatically
 *       flip arguments to ensure the scalar is second. (LLVM may handle this, don't know.)
 *     - Widening and narrowing are supported via the relative_scale field of
 *       the RISCVIntrinsic structure is used to indicate a difference in type
 *       bit wdith, not lanes, between the slot and the smallest size used in the call.
 *       ("Slot" is either return type or an argument.
 *     - Currently this only handles two argument ops.
 *
 * TODO: Curently there are no floating-point intrinsics supported.
 *       Add masking support.
 *       Handle RISC V specific reductions.
 */
bool CodeGen_RISCV::call_riscv_vector_intrinsic(const RISCVIntrinsic &intrin, const Call *op) {
    // This is mostly handled by not having ops that don't take two arguments in
    // the intrinsics tables. However MatchRISCVIntrisic doesn't ensure at least
    // one argument is a vector and it seems likely this guard will grom in the
    // future.
    if (op->args.size() != 2 ||
        (op->args[0].type().is_scalar() && op->args[1].type().is_scalar())) {
        return false;
    }

    // Using vscale types is still highly desirable as LLVM still has
    // instruction selection issues with fixed vector types. The cleanest model
    // would be to use fixed vector types with vector predicated and RISC V
    // specific intrinsics, both of which take a vector length. With the
    // hardware vector register size asserted at a specific width, this should
    // generate the ideal code (for a fixed vector size) as well.
    // For now, this is set to use vscale.
    int scalable = true;
    int effective_vscale = scalable ? target_vscale() : 1;

    int op_max_lanes = vscale_lanes(effective_vscale, op->type);

    for (const Expr &arg : op->args) {
        op_max_lanes = std::max(op_max_lanes,
                                vscale_lanes(effective_vscale, arg.type()));
    }

    Type ret_type = op->type.with_lanes(op_max_lanes);

    llvm::Type *xlen_type = target.bits == 32 ? i32_t : i64_t;

    // Produce intrinsic name and type mangling.
    llvm::Type *llvm_ret_type;
    if (ret_type.is_vector()) {
        int lanes = ret_type.lanes();
        internal_assert(lanes >= effective_vscale) << "Vector type not correctly promoted.\n";
        lanes /= effective_vscale;
        llvm_ret_type = llvm::VectorType::get(llvm_type_of(ret_type.element_of()),
                                              lanes, scalable);
    } else {
        llvm_ret_type = llvm_type_of(ret_type);
    }

    llvm::Value *left_arg = codegen(op->args[0]);
    llvm::Value *right_arg = codegen(op->args[1]);

    internal_assert(!((intrin.flags & ReverseBinOp) &&
                      (intrin.flags & Commutes)))
        << "Cannot have both Commutes and ReverseBinOp set on an intrinsic.\n";

    if (((intrin.flags & Commutes) && op->args[0].type().is_scalar()) ||
        (intrin.flags & ReverseBinOp)) {
        std::swap(left_arg, right_arg);
    }

    // Promote args to vector types if necessary.
    // TODO: break out into method. Scopes are to ensure no temporaries are
    // used elsewhere.
    {
        llvm::Type *left_arg_type = left_arg->getType();
        if (!left_arg_type->isVectorTy()) {
            left_arg = create_broadcast(left_arg, op_max_lanes);
            left_arg_type = left_arg->getType();
        }
        if (llvm::isa<llvm::FixedVectorType>(left_arg_type)) {
            llvm::FixedVectorType *fixed = cast<llvm::FixedVectorType>(left_arg_type);
            left_arg_type = get_vector_type(left_arg_type->getScalarType(),
                                            (fixed->getNumElements() + effective_vscale - 1) / effective_vscale,
                                            VectorTypeConstraint::VScale);
        }
        left_arg = convert_fixed_or_scalable_vector_type(left_arg, left_arg_type);
    }
    {
        llvm::Type *right_arg_type = right_arg->getType();
        if (llvm::isa<llvm::FixedVectorType>(right_arg_type)) {
            llvm::FixedVectorType *fixed = cast<llvm::FixedVectorType>(right_arg_type);
            right_arg_type = get_vector_type(right_arg_type->getScalarType(),
                                             (fixed->getNumElements() + effective_vscale - 1) / effective_vscale,
                                             VectorTypeConstraint::VScale);
        }
        if (right_arg_type->isVectorTy()) {
            right_arg = convert_fixed_or_scalable_vector_type(right_arg, right_arg_type);
        }
    }

    const bool round_down = (intrin.flags & RoundDown) != 0;
    const bool round_up = (intrin.flags & RoundUp) != 0;
    const bool round_any = round_down || round_up;
    internal_assert(!(round_down && round_up));

    // This is the vector tail argument that provides values for uncomputed but
    // within the type length values in the result. This is always passed as
    // undef here.
    std::vector<llvm::Type *> llvm_arg_types = {
        llvm_ret_type,
        left_arg->getType(),
        right_arg->getType(),
    };
#if LLVM_VERSION >= 170
    if (round_any) {
        llvm_arg_types.push_back(xlen_type);
    }
#endif
    if (intrin.flags & AddVLArg) {
        llvm_arg_types.push_back(xlen_type);
    }

    // Build the mangled name for the intrinsic.
    std::string mangled_name = "llvm.riscv.";
    mangled_name += intrin.riscv_name;
    if (intrin.flags & MangleReturnType) {
        mangled_name += mangle_llvm_type(llvm_ret_type);
    }
    mangled_name += mangle_llvm_type(llvm_arg_types[1]);
    mangled_name += mangle_llvm_type(llvm_arg_types[2]);
    if (intrin.flags & AddVLArg) {
        mangled_name += (target.bits == 64) ? ".i64" : ".i32";
    }

    llvm::Function *llvm_intrinsic = get_llvm_intrin(llvm_ret_type, mangled_name, llvm_arg_types);

    // TODO: Should handle intrinsics other than binary operators.
    // Call the LLVM intrinsic.
    const int actual_lanes = op->type.lanes();
    llvm::Constant *actual_vlen = llvm::ConstantInt::get(xlen_type, actual_lanes);

    // See https://github.com/riscv/riscv-v-spec/releases/download/v1.0/riscv-v-spec-1.0.pdf page 15
    // for discussion of fixed-point rounding mode.
    llvm::Value *rounding_mode = llvm::ConstantInt::get(xlen_type, round_down ? 2 : 0);

    // Build the list of call args.
    std::vector<llvm::Value *> call_args = {
        llvm::UndefValue::get(llvm_ret_type),
        left_arg,
        right_arg,
    };
#if LLVM_VERSION >= 170
    // LLVM 17+ has "intrinsics" that set csrw internally; the rounding_mode is before vlen.
    if (round_any) {
        call_args.push_back(rounding_mode);
    }
#else
    // LLVM 16 requires explicitly setting csrw before calling the intrinsic
    if (round_any) {
        // Set vector fixed-point rounding flag for intrinsic.
        llvm::FunctionType *csrw_llvm_type = llvm::FunctionType::get(void_t, {xlen_type}, false);
        llvm::InlineAsm *inline_csrw = llvm::InlineAsm::get(csrw_llvm_type, "csrw vxrm,${0:z}", "rJ,~{memory}", true);
        builder->CreateCall(inline_csrw, {rounding_mode});
    }
#endif
    call_args.push_back(actual_vlen);

    // Finally, make the call.
    value = builder->CreateCall(llvm_intrinsic, call_args);

    if (ret_type.lanes() != op->type.lanes()) {
        value = convert_fixed_or_scalable_vector_type(value,
                                                      get_vector_type(llvm_type_of(op->type.element_of()),
                                                                      op->type.lanes()));
    }

    return true;
}

}  // anonymous namespace

std::unique_ptr<CodeGen_Posix> new_CodeGen_RISCV(const Target &target) {
    return std::make_unique<CodeGen_RISCV>(target);
}

#else  // WITH_RISCV

std::unique_ptr<CodeGen_Posix> new_CodeGen_RISCV(const Target &target) {
    user_error << "RISCV not enabled for this build of Halide.\n";
    return nullptr;
}

#endif  // WITH_RISCV

}  // namespace Internal
}  // namespace Halide
