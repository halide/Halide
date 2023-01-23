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

Type concretize_fixed_or_scalable(const IntrinsicArgPattern &f_or_v, int type_width_scale, int vector_bits) {
    if (f_or_v.type_pattern == IntrinsicArgPattern::Fixed) {
        return f_or_v.type;
    }
    int bit_width = f_or_v.type.bits() * f_or_v.relative_scale * type_width_scale;
    return Type(f_or_v.type.code(), bit_width, (vector_bits * f_or_v.relative_scale) / bit_width);
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
    llvm::Function *define_riscv_intrinsic_wrapper(const RISCVIntrinsic &intrin,
                                                   int type_width_scale);

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

const RISCVIntrinsic intrinsic_defs[] = {
    {"vaadd", Type::Int, "halving_add", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundDown},
    {"vaaddu", Type::UInt, "halving_add", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundDown},
    {"vaadd", Type::Int, "rounding_halving_add", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundUp},
    {"vaaddu", Type::UInt, "rounding_halving_add", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundUp},
    {"vwadd", {Type::Int, 2}, "widening_add", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwaddu", {Type::UInt, 2}, "widening_add", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwsub", {Type::Int, 2}, "widening_sub", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwsubu", {Type::UInt, 2}, "widening_sub", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwmul", {Type::Int, 2}, "widening_mul", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwmulu", {Type::UInt, 2}, "widening_mul", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwmulsu", {Type::Int, 2}, "widening_mul", {Type::Int, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwmulsu", {Type::Int, 2}, "widening_mul", {Type::UInt, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType | RISCVIntrinsic::ReverseBinOp},
};

void CodeGen_RISCV::init_module() {
    CodeGen_Posix::init_module();

    int effective_vscale = target_vscale();
    if (effective_vscale != 0) {
        for (const RISCVIntrinsic &intrin : intrinsic_defs) {
            std::vector<Type> arg_types;
            arg_types.reserve(max_intrinsic_args);
            if (intrin.ret_type.type_pattern == IntrinsicArgPattern::AllTypeWidths) {
                // Iterate over 8/16/32/64 bit integer type widths via log2 shift amount.
                // TODO: Will need to add floating point bit widths when an intrinsic is added.
                //     Not doing this now as it is there would be no coverage, it requires
                //     deciding whether to get floatness from an argument or return type,
                //     and it probably has to check target flags to figure out Float(16)
                //     and BFloat(16) availability.
                for (int log2_of_scale = 0; log2_of_scale < 4; log2_of_scale++) {
                    int bit_width_scale = 1 << log2_of_scale;

                    Type ret_type = concretize_fixed_or_scalable(intrin.ret_type, bit_width_scale,
                                                                 target.vector_bits);

                    if ((intrin.ret_type.relative_scale * bit_width_scale * intrin.ret_type.type.bits()) > 64) {
                        break;
                    }

                    for (const auto &arg_type : intrin.arg_types) {
                        if (arg_type.type_pattern == IntrinsicArgPattern::Undefined) {
                            break;
                        }
                        if ((arg_type.relative_scale * bit_width_scale * arg_type.type.bits()) > 64) {
                            break;
                        }
                        arg_types.push_back(concretize_fixed_or_scalable(arg_type, bit_width_scale,
                                                                         target.vector_bits));
                    }
                    llvm::Function *intrin_impl = define_riscv_intrinsic_wrapper(intrin, bit_width_scale);
                    declare_intrin_overload(intrin.name, ret_type, intrin_impl, arg_types);
                    arg_types.clear();
                }
            } else {
                llvm::Function *intrin_impl = define_riscv_intrinsic_wrapper(intrin, 1);
                Type ret_type = concretize_fixed_or_scalable(intrin.ret_type, 1,
                                                             target.vector_bits);
                for (const auto &arg_type : intrin.arg_types) {
                    if (arg_type.type_pattern == IntrinsicArgPattern::Undefined) {
                        break;
                    }
                    arg_types.push_back(concretize_fixed_or_scalable(arg_type, 1, target.vector_bits));
                }
                declare_intrin_overload(intrin.name, ret_type, intrin_impl, arg_types);
                arg_types.clear();
            }
        }
    }
}

llvm::Function *CodeGen_RISCV::define_riscv_intrinsic_wrapper(const RISCVIntrinsic &intrin,
                                                              int bit_width_scale) {
    int effective_vscale = target_vscale();

    llvm::Type *xlen_type = target.bits == 32 ? i32_t : i64_t;

    // Produce intrinsic name and type mangling.
    std::vector<llvm::Type *> llvm_arg_types;
    std::string mangled_name = "llvm.riscv.";
    mangled_name += intrin.riscv_name;
    Type ret_type = concretize_fixed_or_scalable(intrin.ret_type, bit_width_scale,
                                                 target.vector_bits);
    if (intrin.flags & RISCVIntrinsic::MangleReturnType) {
        bool scalable = (intrin.ret_type.type_pattern != IntrinsicArgPattern::Fixed);
        mangled_name += "." + mangle_vector_argument_type(ret_type, scalable, effective_vscale);
    }
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

    llvm_arg_types.push_back(llvm_ret_type);
    for (const auto &arg_type_pattern : intrin.arg_types) {
        if (arg_type_pattern.type_pattern == IntrinsicArgPattern::Undefined) {
            break;
        }
        Type arg_type = concretize_fixed_or_scalable(arg_type_pattern, bit_width_scale, target.vector_bits);

        bool scalable = (arg_type_pattern.type_pattern != IntrinsicArgPattern::Fixed);
        mangled_name += "." + mangle_vector_argument_type(arg_type, scalable, effective_vscale);
        llvm::Type *llvm_type;
        if (arg_type.is_vector()) {
            int lanes = arg_type.lanes();
            if (scalable) {
                lanes /= effective_vscale;
            }
            llvm_type = llvm::VectorType::get(llvm_type_of(arg_type.element_of()),
                                              lanes, scalable);
        } else {
            llvm_type = llvm_type_of(arg_type);
        }
        llvm_arg_types.push_back(llvm_type);
    }
    if (intrin.flags & RISCVIntrinsic::ReverseBinOp) {
        internal_assert(llvm_arg_types.size() > 2);
        std::swap(llvm_arg_types[1], llvm_arg_types[2]);
    }
    if (intrin.flags & RISCVIntrinsic::AddVLArg) {
        mangled_name += (target.bits == 64) ? ".i64" : ".i32";
        llvm_arg_types.push_back(xlen_type);
    }

    llvm::Function *inner =
        get_llvm_intrin(llvm_ret_type, mangled_name, llvm_arg_types);
    llvm::FunctionType *inner_ty = inner->getFunctionType();

    // Remove vector tail preservation argument.
    llvm_arg_types.erase(llvm_arg_types.begin());
    // Remove vector length argument passed to inrinsic for wrapper.
    // Wrapper will supply a constant for the fixed vector length.
    if (intrin.flags & RISCVIntrinsic::AddVLArg) {
        llvm_arg_types.resize(llvm_arg_types.size() - 1);
    }

    string wrapper_name = unique_name(std::string(intrin.name) + "_wrapper");
    llvm::FunctionType *wrapper_ty = llvm::FunctionType::get(
        inner_ty->getReturnType(), llvm_arg_types, false);
    llvm::Function *wrapper =
        llvm::Function::Create(wrapper_ty, llvm::GlobalValue::InternalLinkage,
                               wrapper_name, module.get());
    llvm::BasicBlock *block =
        llvm::BasicBlock::Create(module->getContext(), "entry", wrapper);
    llvm::IRBuilderBase::InsertPoint here = builder->saveIP();
    builder->SetInsertPoint(block);

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

    // Call the LLVM intrinsic.
    int actual_lanes = ret_type.lanes();
    llvm::Constant *vtype = llvm::ConstantInt::get(xlen_type, actual_lanes);
    // Add an initial argument to handle tail propagation. Only done if result is vector type.
    int left_arg = 0;
    int right_arg = 1;
    if (intrin.flags & RISCVIntrinsic::ReverseBinOp) {
        std::swap(left_arg, right_arg);
    }
    llvm::Value *ret = builder->CreateCall(inner, {llvm::UndefValue::get(llvm_ret_type),
                                                   wrapper->getArg(left_arg), wrapper->getArg(right_arg),
                                                   vtype});
    builder->CreateRet(ret);

    // Always inline these wrappers.
    wrapper->addFnAttr(llvm::Attribute::AlwaysInline);

    builder->restoreIP(here);

    function_does_not_access_memory(wrapper);
    wrapper->addFnAttr(llvm::Attribute::NoUnwind);

    llvm::verifyFunction(*wrapper);
    return wrapper;
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
