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

using std::string;
using std::vector;

#if defined(WITH_RISCV)

namespace {

constexpr int max_intrinsic_args = 4;

struct IntrinsicArgPattern {
    enum TypePattern {
        Undefined,      // Invalid value for sentinel.
        Fixed,          // Argument is a fixed width vector.
        Scalable,       // Argument is a scalable vector.
        AllTypeWidths,  // Argument generalizes to all bit widths of type.
    } type_pattern;

    Type type;
    int relative_scale;

    IntrinsicArgPattern(const Type &type)
        : type_pattern(type.is_vector() ? Fixed : Scalable),
          type(type), relative_scale(1) {
    }
    IntrinsicArgPattern(halide_type_code_t code)
        : type_pattern(AllTypeWidths),
          type(code, 8, 1), relative_scale(1) {
    }
    IntrinsicArgPattern(halide_type_code_t code, int relative_scale)
        : type_pattern(AllTypeWidths),
          type(code, 8, 1), relative_scale(relative_scale) {
    }
    IntrinsicArgPattern()
        : type_pattern(Undefined),
          type(), relative_scale(0) {
    }
};

struct RISCVIntrinsic {
    const char *riscv_name;
    IntrinsicArgPattern ret_type;
    const char *name;
    IntrinsicArgPattern arg_types[max_intrinsic_args];
    int flags;
    enum {
        AddVLArg = 1 << 0,          // Add a constant full size vector length argument
        RoundDown = 1 << 1,         // Set vxrm rounding mode to down (rdn) before intrinsic.
        RoundUp = 1 << 2,           // Set vxrm rounding mode to up (rdu) before intrinsic.
        MangleReturnType = 1 << 3,  // Put return type mangling at start of type list.
        ReverseBinOp = 1 << 4,      // Switch first two arguments to handle asymmetric ops.
    };
};

const RISCVIntrinsic signed_intrinsic_defs[] = {
    {"vaadd", Type::Int, "halving_add", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundDown},
    {"vaadd", Type::Int, "rounding_halving_add", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundUp},
    {"vwadd", {Type::Int, 2}, "widening_add", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwsub", {Type::Int, 2}, "widening_sub", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwmul", {Type::Int, 2}, "widening_mul", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
};

const RISCVIntrinsic unsigned_intrinsic_defs[] = {
    {"vaaddu", Type::UInt, "halving_add", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundDown},
    {"vaaddu", Type::UInt, "rounding_halving_add", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundUp},
    {"vwaddu", {Type::UInt, 2}, "widening_add", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwsubu", {Type::UInt, 2}, "widening_sub", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwmulu", {Type::UInt, 2}, "widening_mul", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
};

const RISCVIntrinsic mixed_sign_intrinsic_defs[] = {
    {"vwmulsu", {Type::Int, 2}, "widening_mul", {Type::Int, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwmulsu", {Type::Int, 2}, "widening_mul", {Type::UInt, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType | RISCVIntrinsic::ReverseBinOp},
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

// Produce LLVM IR intrisic type name mangling for Halide type, with vector codegen info provided.
std::string mangle_vector_argument_type(const Type &arg_type, bool scalable, int effective_vscale) {
    std::string result;
    if (arg_type.is_vector()) {
        int lanes = arg_type.lanes();
        if (!scalable) {
            result = "v" + std::to_string(lanes);
        } else {
            result = "nxv" + std::to_string(lanes / effective_vscale);
        }
    }
    if (arg_type.is_int() || arg_type.is_uint()) {
        result += "i";
    } else {
        result += "f";
    }
    result += std::to_string(arg_type.bits());

    return result;
}

/** A code generator that emits RISC-V code from a given Halide stmt. */
class CodeGen_RISCV : public CodeGen_Posix {
public:
    /** Create a RISC-V code generator. Processor features can be
     * enabled using the appropriate flags in the target struct. */
    CodeGen_RISCV(const Target &);

protected:
    using CodeGen_Posix::visit;

    void init_module() override;

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
    void call_riscv_intrinsic(const RISCVIntrinsic &intrin, const Call *op);
};

CodeGen_RISCV::CodeGen_RISCV(const Target &t)
    : CodeGen_Posix(t) {
    use_llvm_vp_intrinsics = true;
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

void CodeGen_RISCV::init_module() {
  CodeGen_Posix::init_module();
}

void CodeGen_RISCV::visit(const Call *op) {
    const RISCVIntrinsic *intrinsic_def = MatchRISCVIntrisic(op);;

    if (intrinsic_def != nullptr) {
      call_riscv_intrinsic(*intrinsic_def, op);
    } else {
        CodeGen_Posix::visit(op);
    }
}

namespace {

// For vscale types, the lane count must be rounded up to a multiple of vscale.
int vscale_lanes(int vscale, const Type &type) {
    return ((type.lanes() + vscale - 1) / vscale) * vscale;
}

}

void CodeGen_RISCV::call_riscv_intrinsic(const RISCVIntrinsic &intrin, const Call *op) {
    // Using vscale types is still highly desirable as LLVM has still
    // instruction selection issues with fixed vector types. The cleanest model
    // would be to use fixed vector types with vector predictated and RISC V
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

    std::string mangled_name = "llvm.riscv.";
    mangled_name += intrin.riscv_name;

    Type ret_type = op->type.with_lanes(op_max_lanes);

    if (intrin.flags & RISCVIntrinsic::MangleReturnType) {
        mangled_name += "." + mangle_vector_argument_type(ret_type, scalable, target_vscale());
    }

    llvm::Type *xlen_type = target.bits == 32 ? i32_t : i64_t;

    // Produce intrinsic name and type mangling.
    llvm::Type *llvm_ret_type;
    if (ret_type.is_vector()) {
        int lanes = ret_type.lanes();
        bool scalable = (intrin.ret_type.type_pattern != IntrinsicArgPattern::Fixed);
        if (scalable) {
            lanes /= effective_vscale;
        }
        llvm_ret_type = llvm::VectorType::get(llvm_type_of(ret_type.element_of()),
                                              lanes, scalable);
    } else {
        llvm_ret_type = llvm_type_of(ret_type);
    }

    llvm::Value *left_arg = codegen(op->args[0]);
    llvm::Value *right_arg = codegen(op->args[1]);;

    // Promote args to vector types if necessary.
    if (!left_arg->getType()->isVectorTy()) {
        llvm::Value *promoted_vector = llvm::PoisonValue::get(llvm_ret_type);
        left_arg = builder->CreateInsertElement(promoted_vector, left_arg,
                                                llvm::ConstantInt::get(i32_t, 0));
    }
    if (!right_arg->getType()->isVectorTy()) {
        llvm::Value *promoted_vector = llvm::PoisonValue::get(llvm_ret_type);
        right_arg = builder->CreateInsertElement(promoted_vector, right_arg,
                                                 llvm::ConstantInt::get(i32_t, 0));
    }

    if (intrin.flags & RISCVIntrinsic::ReverseBinOp) {
        std::swap(left_arg, right_arg);
    }

    // This is the vector tail argument that provides values for uncomputed but
    // within the type length values in the result. This is always passed as
    // undef here.
    std::vector<llvm::Type *> llvm_arg_types;
    llvm_arg_types.push_back(llvm_ret_type);
    llvm_arg_types.push_back(left_arg->getType());
    llvm_arg_types.push_back(right_arg->getType());
    mangled_name += "." + vector_mangle_name(llvm_arg_types[1]);
    mangled_name += "." + vector_mangle_name(llvm_arg_types[2]);

    if (intrin.flags & RISCVIntrinsic::AddVLArg) {
        mangled_name += (target.bits == 64) ? ".i64" : ".i32";
        llvm_arg_types.push_back(xlen_type);
    }

    llvm::Function *llvm_intrinsic =
        get_llvm_intrin(llvm_ret_type, mangled_name, llvm_arg_types);

    // Set vector fixed-point rounding flag if needed for intrinsic.
    bool round_down = intrin.flags & RISCVIntrinsic::RoundDown;
    bool round_up = intrin.flags & RISCVIntrinsic::RoundUp;
    if (round_down || round_up) {
        internal_assert(!(round_down && round_up));
        llvm::Value *rounding_mode = llvm::ConstantInt::get(xlen_type, round_down ? 2 : 0);
        // See https://github.com/riscv/riscv-v-spec/releases/download/v1.0/riscv-v-spec-1.0.pdf page 15
        // for discussion of fixed-point rounding mode.
        // TODO: When LLVM finally fixes the instructions to take rounding modes,
        // this will have to change to passing the rounding mode to the intrinsic.
        // https://github.com/halide/Halide/issues/7123
        llvm::FunctionType *csrw_llvm_type = llvm::FunctionType::get(void_t, {xlen_type}, false);
        llvm::InlineAsm *inline_csrw = llvm::InlineAsm::get(csrw_llvm_type, "csrw vxrm,${0:z}", "rJ,~{memory}", true);
        builder->CreateCall(inline_csrw, {rounding_mode});
    }

    // TODO: Should handle inrinsics other than binary operators.
    // Call the LLVM intrinsic.
    int actual_lanes = op->type.lanes();
    llvm::Constant *vtype = llvm::ConstantInt::get(xlen_type, actual_lanes);

    value = builder->CreateCall(llvm_intrinsic, {llvm::UndefValue::get(llvm_ret_type),
            left_arg, right_arg,
            vtype});

    if (ret_type.lanes() != op->type.lanes()) {
        value = convert_fixed_or_scalable_vector_type(value,
                                                      get_vector_type(llvm_type_of(op->type.element_of()),
                                                                      op->type.lanes()));
    }
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
