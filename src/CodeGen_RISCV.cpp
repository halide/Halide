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
    int relative_scale;

    FixedOrScalableVector(const Type &type, bool scalable = true) :
        type_pattern(type.is_vector() ? Fixed : Scalable),
        type(type), relative_scale(1) {
    }
    FixedOrScalableVector(halide_type_code_t code) :
        type_pattern(WildcardWidths),
        type(code, 8, 1), relative_scale(1) {
    }
    FixedOrScalableVector(halide_type_code_t code, int relative_scale) :
        type_pattern(WildcardWidths),
        type(code, 8, 1), relative_scale(relative_scale) {
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
        MangleReturnType = 1 << 3, // Put return type mangling at start of type list.
    };
};

Type concretize_fixed_or_scalable(const FixedOrScalableVector &f_or_v, int type_width_scale, int vector_bits) {
    if (f_or_v.type_pattern == FixedOrScalableVector::Fixed) {
        return f_or_v.type;
    }
    int bit_width = f_or_v.type.bits() * f_or_v.relative_scale * type_width_scale;
    return Type(f_or_v.type.code(), bit_width, (vector_bits * f_or_v.relative_scale) / bit_width);
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

/** A code generator that emits mips code from a given Halide stmt. */
class CodeGen_RISCV : public CodeGen_Posix {
public:
    /** Create a mips code generator. Processor features can be
     * enabled using the appropriate flags in the target struct. */
    CodeGen_RISCV(const Target &);
    llvm::Function *define_riscv_intrinsic_wrapper(const RISCVIntrinsic &intrin,
                                                   int type_width_scale);

protected:
#if 0
    llvm::Value *CodeGen_RISCV::interleave_vectors(const std::vector<llvm::Value *> &vecs);
#endif

    using CodeGen_Posix::visit;
    void visit(const Call *) override;

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

#if 0
llvm::Value *CodeGen_RISCV::interleave_vectors(const std::vector<Value *> &vecs) {
    internal_assert(!vecs.empty());
    for (size_t i = 1; i < vecs.size(); i++) {
        internal_assert(vecs[0]->getType() == vecs[i]->getType());
    }
    int vec_elements = get_vector_num_elements(vecs[0]->getType());

    int element_bits = vecs[0]->getType().bits();
    if (vecs.size() == 2 &&
        vec_elements < (target.vector_bits / element_bits)) {
        Value *a = vecs[0];
        Value *b = vecs[1];
#if 0
        vector<int> indices(vec_elements * 2);
        for (int i = 0; i < vec_elements * 2; i++) {
            indices[i] = i % 2 == 0 ? i / 2 : i / 2 + vec_elements;
        }
        return shuffle_vectors(a, b, indices);
#else
        llvm::Type *xlen_type = target.bits == 32 ? i32_t : i64_t;
        llvm::Value *vl = llvm::ConstantInt::get(xlen_type, vec_elements);
        llvm::Value *vlx2 = llvm::ConstantInt::get(xlen_type, vec_elements * 2);
        llvm::Type *llvm_elem_type = llvm_type_of(UInt(element_bits));

#if 0
  // Assume vectors to interleave are in v0 and v1.
  vwcvtu.x.x.v v2, v0     // zext v0 into v2 with 0s in bits 31:0 of each resulting element
  li          a0, 65535   // load immediate into a0 since there is no vmaccu.vi instruction.
  vwmaccu.vx  v2, a0, v1  // v2 += (v1 * 65535) // multiply produces a full i32 product. 65535 is the largest constant that we can multiply by for i16
  vwadd.wv    v2, v2, v1  // v2 += v1 // add v1 again so that we’ve multiplied v1 by 65536, equivalent to shift left by 16.
  // bitcast v2 to i16 vector with twice as many element as we started with.
#endif

        // Bit cast to make unit type of correct size
        std::string convert_name =  "llvm.riscv.vwadd." + mangled_double + "." mangled_single;
        llvm::::Function widen_convert = get_llvm_intrinsic(llvm_x2_type, convert_name, { llvm_x2_type, llvm_x1_type, llvm_x2_type, llvm_elem_type });


        llvm::Type *integer_type = llvm::VectorType::get(), 2, true);

        llvm::Value *a = builder->CreateBitCast(&arg, llvm_type_of(args[i].type));

        std::string to_int_name = "llvm.riscv.vfcvt" + rtz_insert + ".x.f.v.nxv2i32.nxv2f32.i64";
        llvm::Function *to_int = get_llvm_intrin(integer_type, to_int_name, { float_type, i64_t });
        internal_assert(to_int != nullptr);
        llvm::Function *to_float = get_llvm_intrin(float_type, "llvm.riscv.vfcvt.f.x.v.nxv2f32.nxv2i32.i64", { integer_type, i64_t });
        llvm::Value *integer_val = builder->CreateCall(to_int, { arg, vl });
        value = builder->CreateCall(to_float, { integer_val, vl });
        value = normalize_fixed_scalable_vector_type(llvm_type_of(op->type), codegen(op->args[0]));

        llvm.riscv.vwadd.vx vd,vs,x0,vm
#endif
    } else {
        return Codegen_Posix::interleave_vectors(vecs);
    }
}
#endif

void CodeGen_RISCV::visit(const Call *op) {
// Temporary conditional for A/B testing performance effect of floor/ceil/etc.
#if 0
    if (op->call_type == Call::PureExtern &&
        (op->name == "floor_f32" ||
         op->name == "ceil_f32" ||
         op->name == "round_f32" ||
         op->name == "trunc_f32")) {
        if (op->type.is_vector() && target.vector_bits != 0 &&
            ((op->type.bits() * op->type.lanes()) == target.vector_bits)) {
            internal_assert(op->type.is_float());
            internal_assert(op->type.bits() == 32);
            internal_assert(op->args.size() == 1);
            internal_assert(op->args[0].type().is_float());
            internal_assert(op->args[0].type().bits() == 32);

            llvm::Type *xlen_type = target.bits == 32 ? i32_t : i64_t;

            llvm::Value *rounding_mode;
            bool set_rounding_mode;
            if (op->name == "floor_f32") {
                rounding_mode = llvm::ConstantInt::get(xlen_type, 2);
                set_rounding_mode = true;
            } else if (op->name == "ceil_f32") {
                rounding_mode = llvm::ConstantInt::get(xlen_type, 3);
                set_rounding_mode = true;
            } else if (op->name == "round_f32") {
                rounding_mode = llvm::ConstantInt::get(xlen_type, 0);
                set_rounding_mode = true;
            } else {  // trunc
                set_rounding_mode = false;
            }
            llvm::Value *vl = llvm::ConstantInt::get(xlen_type, op->type.lanes());
            llvm::Type *float_type = llvm::VectorType::get(f32_t, 2, true);
            llvm::Type *integer_type = llvm::VectorType::get(i32_t, 2, true);
            llvm::Value *codegened = codegen(op->args[0]);
            llvm::Value *arg = normalize_fixed_scalable_vector_type(float_type, codegened);
            llvm::dbgs() << "codegened type is ";
            codegened->getType()->print(llvm::dbgs(), true);
            llvm::dbgs() << " normalized type is ";
            arg->getType()->print(llvm::dbgs(), true);
            llvm::dbgs()  << "\n";

            if (set_rounding_mode) {
                llvm::FunctionType *csrw_llvm_type = llvm::FunctionType::get(void_t, { xlen_type }, false);
                llvm::InlineAsm *inline_csrw = llvm::InlineAsm::get(csrw_llvm_type, "csrwi frm,$0", "i,~{memory}", true);
                builder->CreateCall(inline_csrw, {rounding_mode});
            }

            std::string rtz_insert = set_rounding_mode ? "" : ".rtz";
            std::string to_int_name = "llvm.riscv.vfcvt" + rtz_insert + ".x.f.v.nxv2i32.nxv2f32.i64";
            llvm::Function *to_int = get_llvm_intrin(integer_type, to_int_name, { float_type, i64_t });
            internal_assert(to_int != nullptr);
            llvm::Function *to_float = get_llvm_intrin(float_type, "llvm.riscv.vfcvt.f.x.v.nxv2f32.nxv2i32.i64", { integer_type, i64_t });
            llvm::Value *integer_val = builder->CreateCall(to_int, { arg, vl });
            value = builder->CreateCall(to_float, { integer_val, vl });
            value = normalize_fixed_scalable_vector_type(llvm_type_of(op->type), codegen(op->args[0]));
            return;
        }
    }
#endif

    CodeGen_Posix::visit(op);
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
        arch_flags += ",+v";
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

namespace {

const RISCVIntrinsic intrinsic_defs[] = {
    {"vaadd", Type::Int, "halving_add", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundDown},
    {"vaaddu", Type::UInt, "halving_add", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundDown},
    {"vaadd", Type::Int, "rounding_halving_add", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundUp},
    {"vaaddu", Type::UInt, "rounding_halving_add", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::RoundUp},
    {"vwadd", { Type::Int, 2}, "widening_add", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwaddu", { Type::UInt, 2}, "widening_add", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwsub", { Type::Int, 2}, "widening_sub", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwsubu", { Type::UInt, 2}, "widening_sub", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwmul", { Type::Int, 2}, "widening_mul", {Type::Int, Type::Int}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
    {"vwmulu", { Type::UInt, 2}, "widening_mul", {Type::UInt, Type::UInt}, RISCVIntrinsic::AddVLArg | RISCVIntrinsic::MangleReturnType},
};

void CodeGen_RISCV::init_module() {
    CodeGen_Posix::init_module();

    int effective_vscale = target_vscale();
    if (effective_vscale != 0) {
        for (const RISCVIntrinsic &intrin : intrinsic_defs) {
            if (intrin.ret_type.type_pattern == FixedOrScalableVector::WildcardWidths) {
                for (int scale = 0; scale < 4; scale++) {
                    int bit_width_scale = 1 << scale;

                    Type ret_type = concretize_fixed_or_scalable(intrin.ret_type, bit_width_scale,
                                                                 target.vector_bits);

                    std::vector<Type> arg_types;
                    if ((intrin.ret_type.relative_scale * bit_width_scale * intrin.ret_type.type.bits()) > 64) {
                        break;
                    }

                    // Add an initial argument to handle tail propagation.
                    arg_types.push_back(ret_type);

                    for (const auto &arg_type : intrin.arg_types) {
                        if (arg_type.type_pattern == FixedOrScalableVector::Undefined) {
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
                }
            } else {
                llvm::Function *intrin_impl = define_riscv_intrinsic_wrapper(intrin, 1);
                Type ret_type = concretize_fixed_or_scalable(intrin.ret_type, 1,
                                                             target.vector_bits);
                std::vector<Type> arg_types;

                // Add an initial argument to handle tail propagation.
                arg_types.push_back(ret_type);

                for (const auto &arg_type : intrin.arg_types) {
                    if (arg_type.type_pattern == FixedOrScalableVector::Undefined) {
                        break;
                    }
                    arg_types.push_back(concretize_fixed_or_scalable(arg_type, 1, target.vector_bits));
                }
                declare_intrin_overload(intrin.name, ret_type, intrin_impl, arg_types);
            }
        }
    }
}

llvm::Function *CodeGen_RISCV::define_riscv_intrinsic_wrapper(const RISCVIntrinsic &intrin,
                                                              int bit_width_scale) {
    int effective_vscale = target_vscale();

    llvm::Type *xlen_type = target.bits == 32 ? i32_t : i64_t;

    // Produce intrinsic name and
    std::vector<llvm::Type *> llvm_arg_types;
    std::string mangled_name = "llvm.riscv.";
    mangled_name += intrin.riscv_name;
    Type ret_type = concretize_fixed_or_scalable(intrin.ret_type, bit_width_scale,
                                                 target.vector_bits);
    if (intrin.flags & RISCVIntrinsic::MangleReturnType) {
        bool scalable = (intrin.ret_type.type_pattern != FixedOrScalableVector::Fixed);
        mangled_name += "." + mangle_vector_argument_type(ret_type, scalable, effective_vscale);
    }
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

    llvm_arg_types.push_back(llvm_ret_type);
    for (const auto &arg_type_pattern : intrin.arg_types) {
        if (arg_type_pattern.type_pattern == FixedOrScalableVector::Undefined) {
            break;
        }
        Type arg_type = concretize_fixed_or_scalable(arg_type_pattern, bit_width_scale, target.vector_bits);

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
// LLVM support for intrinsic is not in yet, but there is a PR in review
#if 0
        llvm::FunctionType *csrw_llvm_type = llvm::FunctionType::get(void_t, { xlen_type }, false);
        llvm::InlineAsm *inline_csrw = llvm::InlineAsm::get(csrw_llvm_type, "csrw vxrm,${0:z}", "rJ,~{memory}", true);
        builder->CreateCall(inline_csrw, {rounding_mode});
#else
        std::string name = "llvm.riscv.vsetvxrm.i" + std::to_string(target.bits);
        (void)call_intrin(void_t, 1, name, { rounding_mode });
#endif
    }

    // Call the LLVM intrinsic.
    // TODO: This does not work for reductions.
    int actual_lanes = ret_type.lanes();
    llvm::Constant *vtype = llvm::ConstantInt::get(xlen_type, actual_lanes);
    // Add an initial argument to handle tail propagation. Only done if result is vector type.
    llvm::Value *ret = builder->CreateCall(inner, {llvm::UndefValue::get(llvm_ret_type), wrapper->getArg(0), wrapper->getArg(1), vtype});

    builder->CreateRet(ret);

    // Always inline these wrappers.
    wrapper->addFnAttr(llvm::Attribute::AlwaysInline);

    builder->restoreIP(here);

    wrapper->addFnAttr(llvm::Attribute::ReadNone);
    wrapper->addFnAttr(llvm::Attribute::NoUnwind);

    // llvm::verifyFunction(*wrapper);
    return wrapper;
}

}  // anonymous namepspace

#else  // WITH_RISCV

std::unique_ptr<CodeGen_Posix> new_CodeGen_RISCV(const Target &target) {
    user_error << "RISCV not enabled for this build of Halide.\n";
    return nullptr;
}

#endif  // WITH_RISCV

}  // namespace Internal
}  // namespace Halide
