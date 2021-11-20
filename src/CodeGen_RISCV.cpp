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

struct FixedOrScalableVector {
    enum TypePattern {
        Undefined,
        Fixed,
        Scalable,
        WildcardWidths,
    } type_pattern;
      
    
    Type type;

    FixedOrScalableVector(const Type &type, bool scalable = true) :
        type_pattern(type.is_vector() ? Fixed : Scalable),
        type(type) {
    }
    FixedOrScalableVector(halide_type_code_t code) :
        type_pattern(WildcardWidths),
        type(code, 8, 1) {
    }
    FixedOrScalableVector() :
        type_pattern(Undefined),
        type() {
    }
};
  
struct RISCVIntrinsic {
    const char *riscv_name;
    FixedOrScalableVector ret_type;
    const char *name;
    FixedOrScalableVector arg_types[max_intrinsic_args];
    int flags;
    enum {
        AddVLArg = 1 << 0,   // Add a constant full size vector length argument
        RoundDown = 1 << 1,   // Set rounding mode to down (rdn) before intrinsic.
        RoundUp = 1 << 2,   // Set rounding mode to up (rdu) before intrinsic.
    };
};

/** A code generator that emits mips code from a given Halide stmt. */
class CodeGen_RISCV : public CodeGen_Posix {
public:
    /** Create a mips code generator. Processor features can be
     * enabled using the appropriate flags in the target struct. */
    CodeGen_RISCV(const Target &);
  llvm::Function *define_riscv_intrinsic_wrapper(const RISCVIntrinsic &intrin, int bit_width);

protected:
    using CodeGen_Posix::visit;

    void init_module() override;

    string mcpu() const override;
    string mattrs() const override;
    string mabi() const override;
    bool use_soft_float_abi() const override;
    int native_vector_bits() const override;
    int target_vscale() const override;
};

CodeGen_RISCV::CodeGen_RISCV(const Target &t)
    : CodeGen_Posix(t) {
}

string CodeGen_RISCV::mcpu() const {
    return "";
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
        arch_flags += ",+experimental-v";
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

int CodeGen_RISCV::target_vscale() const {
    if (target.vector_bits != 0 &&
        target.has_feature(Target::RVV)) {
        internal_assert((target.vector_bits % 64) == 0);
        return target.vector_bits / 64;
    }

    return 0;
}

}  // namespace

std::unique_ptr<CodeGen_Posix> new_CodeGen_RISCV(const Target &target) {
    return std::make_unique<CodeGen_RISCV>(target);
}

const RISCVIntrinsic intrinsic_defs[] = {
    {"vaadd", Type::Int, "halving_add", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundDown},
    {"vaaddu", Type::UInt, "halving_add", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundDown},
    {"vaadd", Type::Int, "rounding_halving_add", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundUp},
    {"vaaddu", Type::UInt, "rounding_halving_add", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundUp},
};

namespace {

Type concretize_fixed_or_scalable(const FixedOrScalableVector &f_or_v, int bit_width, int vector_bits) {
    if (f_or_v.type_pattern == FixedOrScalableVector::Fixed) {
        return f_or_v.type;
    }
    return Type(f_or_v.type.code(), bit_width, vector_bits / bit_width);
}

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

}

void CodeGen_RISCV::init_module() {
    CodeGen_Posix::init_module();

    int effective_vscale = target_vscale();
    if (effective_vscale != 0) {
        for (const RISCVIntrinsic &intrin : intrinsic_defs) {
            if (intrin.ret_type.type_pattern == FixedOrScalableVector::WildcardWidths) {
                for (int scale = 0; scale < 4; scale++) {
                    int bit_width = 8 << scale;

                    llvm::Function *intrin_impl = define_riscv_intrinsic_wrapper(intrin, bit_width);
                    Type ret_type = concretize_fixed_or_scalable(intrin.ret_type, bit_width,
                                                                 target.vector_bits);
                    std::vector<Type> arg_types;
                    for (const auto &arg_type : intrin.arg_types) {
                        if (arg_type.type_pattern == FixedOrScalableVector::Undefined) {
                            break;
                        }
                        arg_types.push_back(concretize_fixed_or_scalable(arg_type, bit_width,
                                                                         target.vector_bits));
                    }
                    declare_intrin_overload(intrin.name, ret_type, intrin_impl, arg_types);
                }
            } else {
                llvm::Function *intrin_impl = define_riscv_intrinsic_wrapper(intrin, intrin.ret_type.type.bits());
                Type ret_type = concretize_fixed_or_scalable(intrin.ret_type, intrin.ret_type.type.bits(),
                                                               target.vector_bits);
                std::vector<Type> arg_types;
                for (const auto &arg_type : intrin.arg_types) {
                    if (arg_type.type_pattern == FixedOrScalableVector::Undefined) {
                        break;
                    }
                    arg_types.push_back(concretize_fixed_or_scalable(arg_type, arg_type.type.bits(),
                                                                       target.vector_bits));
                }
                declare_intrin_overload(intrin.name, ret_type, intrin_impl, arg_types);
            }
        }
    }
}

llvm::Function *CodeGen_RISCV::define_riscv_intrinsic_wrapper(const RISCVIntrinsic &intrin,
                                                              int bit_width) {
    int effective_vscale = target_vscale();

    llvm::Type *xlen_type = target.bits == 32 ? i32_t : i64_t;

    // Produce intrinsic name and
    std::vector<llvm::Type *> llvm_arg_types;
    std::string mangled_name = "llvm.riscv.";
    mangled_name += intrin.riscv_name;
    for (const auto &arg_type_pattern : intrin.arg_types) {
        if (arg_type_pattern.type_pattern == FixedOrScalableVector::Undefined) {
            break;
        }
        Type arg_type = concretize_fixed_or_scalable(arg_type_pattern, bit_width, target.vector_bits);

        bool scalable = (arg_type_pattern.type_pattern != FixedOrScalableVector::Fixed);
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
    if (intrin.flags & RISCVIntrinsic::AddVLArg) {
        mangled_name += (target.bits == 64) ? ".i64" : ".i32";
        llvm_arg_types.push_back(xlen_type);
    }

    Type ret_type = concretize_fixed_or_scalable(intrin.ret_type, bit_width,
                                                 target.vector_bits);
    llvm::Type *llvm_ret_type;
    if (ret_type.is_vector()) {
        int lanes = ret_type.lanes();
        bool scalable = (intrin.ret_type.type_pattern != FixedOrScalableVector::Fixed);
        if (scalable) {
             lanes /= effective_vscale;
        }
        llvm_ret_type = llvm::VectorType::get(llvm_type_of(ret_type.element_of()),
                                              lanes, scalable);
    } else {
        llvm_ret_type = llvm_type_of(ret_type);
    }
      
    llvm::Function *inner =
        get_llvm_intrin(llvm_ret_type, mangled_name, llvm_arg_types);
    llvm::FunctionType *inner_ty = inner->getFunctionType();

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
// LLVM support for intrinsic is not in yet, but there is a PR in review
#if 1
        llvm::FunctionType *csrw_llvm_type = llvm::FunctionType::get(void_t, { xlen_type }, false);
        llvm::InlineAsm *inline_csrw = llvm::InlineAsm::get(csrw_llvm_type, "csrw vxrm,${0:z}", "rJ,~{memory}", true);
        builder->CreateCall(inline_csrw, {rounding_mode});
#else
        std::string name = "llvm.riscv.vsetvxrm.i" + std::to_string(target.bits);
        (void)call_intrin(void_t, 1, name, { rounding_mode });
#endif
    }

    // Call the LLVM intrinsic.
    int actual_lanes = ret_type.lanes();
    llvm::Constant *vtype = llvm::ConstantInt::get(xlen_type, actual_lanes);
    llvm::Value *ret = builder->CreateCall(
        inner, {wrapper->getArg(0), wrapper->getArg(1), vtype});

    builder->CreateRet(ret);

    // Always inline these wrappers.
    wrapper->addFnAttr(llvm::Attribute::AlwaysInline);

    builder->restoreIP(here);

    wrapper->addFnAttr(llvm::Attribute::ReadNone);
    wrapper->addFnAttr(llvm::Attribute::NoUnwind);

    // llvm::verifyFunction(*wrapper);
    return wrapper;
}

#else  // WITH_RISCV

std::unique_ptr<CodeGen_Posix> new_CodeGen_RISCV(const Target &target) {
    user_error << "RISCV not enabled for this build of Halide.\n";
    return nullptr;
}

#endif  // WITH_RISCV

}  // namespace Internal
}  // namespace Halide
