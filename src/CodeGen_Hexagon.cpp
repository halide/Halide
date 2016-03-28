#include <iostream>
#include <sstream>

#include "LLVM_Headers.h"
#include "CodeGen_Hexagon.h"
#include "IROperator.h"
#include "IRMatch.h"
#include "IREquality.h"
#include "Target.h"
#include "Debug.h"
#include "Util.h"
#include "Simplify.h"
#include "IRPrinter.h"
#include "EliminateBoolVectors.h"
#include "HexagonOptimize.h"

#define IPICK(i64) (B128 ? i64##_128B : i64)

namespace Halide {
namespace Internal {

using std::vector;
using std::string;
using std::ostringstream;
using std::pair;
using std::make_pair;

using namespace llvm;

CodeGen_Hexagon::CodeGen_Hexagon(Target t) : CodeGen_Posix(t) {}

std::unique_ptr<llvm::Module> CodeGen_Hexagon::compile(const Module &module) {
    auto llvm_module = CodeGen_Posix::compile(module);
    static bool options_processed = false;

    // TODO: This should be set on the module itself, or some other
    // safer way to pass this through to the target specific lowering
    // passes. We set the option here (after the base class'
    // implementaiton of compile) because it is the last
    // Hexagon-specific code to run prior to invoking the target
    // specific lowering in LLVM, minimizing the chances of the wrong
    // flag being set for the wrong module.
    if (!options_processed) {
      cl::ParseEnvironmentOptions("halide-hvx-be", "HALIDE_LLVM_ARGS",
                                  "Halide HVX internal compiler\n");
      // We need to EnableQuIC for LLVM and Halide (Unrolling).
      char *s = strdup("HALIDE_LLVM_QUIC=-hexagon-small-data-threshold=0");
      ::putenv(s);
      cl::ParseEnvironmentOptions("halide-hvx-be", "HALIDE_LLVM_QUIC",
                                  "Halide HVX quic option\n");
    }
    options_processed = true;

    if (module.target().has_feature(Halide::Target::HVX_128)) {
        char *s = strdup("HALIDE_LLVM_INTERNAL=-enable-hexagon-hvx-double");
        ::putenv(s);
        cl::ParseEnvironmentOptions("halide-hvx-be", "HALIDE_LLVM_INTERNAL",
                                    "Halide HVX internal options\n");
        if (module.target().has_feature(Halide::Target::HVX_64))
                internal_error << "Both HVX_64 and HVX_128 set at same time\n";
    }
    return llvm_module;
}

void CodeGen_Hexagon::compile_func(const LoweredFunc &f) {
    CodeGen_Posix::begin_func(f.linkage, f.name, f.args);

    // Modify the body to add a call to halide_qurt_hvx_lock, and
    // register a destructor to call halide_qurt_hvx_unlock.
    Expr hvx_mode = target.has_feature(Target::HVX_128) ? 128 : 64;
    Expr hvx_lock = Call::make(Int(32), "halide_qurt_hvx_lock", {hvx_mode}, Call::Extern);
    string hvx_lock_result_name = unique_name("hvx_lock_result");
    Expr hvx_lock_result_var = Variable::make(Int(32), hvx_lock_result_name);
    Stmt check_hvx_lock = LetStmt::make(hvx_lock_result_name, hvx_lock,
                                        AssertStmt::make(EQ::make(hvx_lock_result_var, 0), hvx_lock_result_var));

    Expr dummy_obj = reinterpret(Handle(), cast<uint64_t>(1));
    Expr hvx_unlock = Call::make(Int(32), Call::register_destructor,
                                 {Expr("halide_qurt_hvx_unlock_as_destructor"), dummy_obj}, Call::Intrinsic);

    Stmt body = f.body;
    body = Block::make(Evaluate::make(hvx_unlock), body);
    body = Block::make(check_hvx_lock, body);

    // We can't deal with bool vectors, convert them to integer vectors.
    debug(1) << "Eliminating boolean vectors from Hexagon code...\n";
    body = eliminate_bool_vectors(body);
    debug(2) << "Lowering after eliminating boolean vectors: " << body << "\n\n";

    // Optimize the IR for Hexagon.
    debug(1) << "Optimizing Hexagon code...\n";
    body = optimize_hexagon(body, target);

    debug(1) << "Hexagon function body:\n";
    debug(1) << body << "\n";

    body.accept(this);

    CodeGen_Posix::end_func(f.args);
}

void CodeGen_Hexagon::init_module() {
    CodeGen_Posix::init_module();

    bool B128 = target.has_feature(Halide::Target::HVX_128);

    Type i8 = Int(8);
    Type i16 = Int(16);
    Type i32 = Int(32);
    Type u8 = UInt(8);
    Type u16 = UInt(16);
    Type u32 = UInt(32);

    // Define some confusingly named vectors that are 1x and 2x the
    // Hexagon HVX width.
    Type i8x1 = i8.with_lanes(native_vector_bits() / 8);
    Type i16x1 = i16.with_lanes(native_vector_bits() / 16);
    Type i32x1 = i32.with_lanes(native_vector_bits() / 32);
    Type u8x1 = u8.with_lanes(native_vector_bits() / 8);
    Type u16x1 = u16.with_lanes(native_vector_bits() / 16);
    Type u32x1 = u32.with_lanes(native_vector_bits() / 32);

    Type i8x2 = i8x1.with_lanes(i8x1.lanes() * 2);
    Type i16x2 = i16x1.with_lanes(i16x1.lanes() * 2);
    Type i32x2 = i32x1.with_lanes(i32x1.lanes() * 2);
    Type u8x2 = u8x1.with_lanes(u8x1.lanes() * 2);
    Type u16x2 = u16x1.with_lanes(u16x1.lanes() * 2);
    Type u32x2 = u32x1.with_lanes(u32x1.lanes() * 2);

    // LLVM's HVX vector intrinsics don't include the type of the
    // operands, they all operate on 32 bit integer vectors. To make
    // it easier to generate code, we define wrapper intrinsics with
    // the correct type (plus the necessary bitcasts).
    struct HvxIntrinsic {
        Intrinsic::ID id;
        Type ret_type;
        const char *name;
        std::vector<Type> arg_types;
    };
    HvxIntrinsic intrinsic_wrappers[] = {
        // Zero/sign extension:
        { IPICK(Intrinsic::hexagon_V6_vzb), u16x2,  "vzxt.ub", {u8x1} },
        { IPICK(Intrinsic::hexagon_V6_vzh), u32x2,  "vzxt.uh", {u16x1} },
        { IPICK(Intrinsic::hexagon_V6_vsb), i16x2,  "vsxt.b",  {i8x1} },
        { IPICK(Intrinsic::hexagon_V6_vsh), i32x2,  "vsxt.h",  {i16x1} },

        // Adds/subtracts:
        // Note that we just use signed arithmetic for unsigned
        // operands, because it works with two's complement arithmetic.
        { IPICK(Intrinsic::hexagon_V6_vaddb),     i8x1,  "vadd.b",     {i8x1,  i8x1} },
        { IPICK(Intrinsic::hexagon_V6_vaddh),     i16x1, "vadd.h",     {i16x1, i16x1} },
        { IPICK(Intrinsic::hexagon_V6_vaddw),     i32x1, "vadd.w",     {i32x1, i32x1} },
        { IPICK(Intrinsic::hexagon_V6_vaddb_dv),  i8x2,  "vadd.b.dv",  {i8x2,  i8x2} },
        { IPICK(Intrinsic::hexagon_V6_vaddh_dv),  i16x2, "vadd.h.dv",  {i16x2, i16x2} },
        { IPICK(Intrinsic::hexagon_V6_vaddw_dv),  i32x2, "vadd.w.dv",  {i32x2, i32x2} },

        { IPICK(Intrinsic::hexagon_V6_vsubb),     i8x1,  "vsub.b",     {i8x1,  i8x1} },
        { IPICK(Intrinsic::hexagon_V6_vsubh),     i16x1, "vsub.h",     {i16x1, i16x1} },
        { IPICK(Intrinsic::hexagon_V6_vsubw),     i32x1, "vsub.w",     {i32x1, i32x1} },
        { IPICK(Intrinsic::hexagon_V6_vsubb_dv),  i8x2,  "vsub.b.dv",  {i8x2,  i8x2} },
        { IPICK(Intrinsic::hexagon_V6_vsubh_dv),  i16x2, "vsub.h.dv",  {i16x2, i16x2} },
        { IPICK(Intrinsic::hexagon_V6_vsubw_dv),  i32x2, "vsub.w.dv",  {i32x2, i32x2} },


        // Adds/subtract of unsigned values with saturation.
        { IPICK(Intrinsic::hexagon_V6_vaddubsat),    u8x1,  "vaddsat.ub",    {u8x1,  u8x1} },
        { IPICK(Intrinsic::hexagon_V6_vadduhsat),    u16x1, "vaddsat.uh",    {u16x1, u16x1} },
        { IPICK(Intrinsic::hexagon_V6_vaddhsat),     i16x1, "vaddsat.h",     {i16x1, i16x1} },
        { IPICK(Intrinsic::hexagon_V6_vaddwsat),     i32x1, "vaddsat.w",     {i32x1, i32x1} },
        { IPICK(Intrinsic::hexagon_V6_vaddubsat_dv), u8x2,  "vaddsat.ub.dv", {u8x2,  u8x2} },
        { IPICK(Intrinsic::hexagon_V6_vadduhsat_dv), u16x2, "vaddsat.uh.dv", {u16x2, u16x2} },
        { IPICK(Intrinsic::hexagon_V6_vaddhsat_dv),  i16x2, "vaddsat.h.dv",  {i16x2, i16x2} },
        { IPICK(Intrinsic::hexagon_V6_vaddwsat_dv),  i32x2, "vaddsat.w.dv",  {i32x2, i32x2} },

        { IPICK(Intrinsic::hexagon_V6_vsububsat),    u8x1,  "vsubsat.ub",    {u8x1,  u8x1} },
        { IPICK(Intrinsic::hexagon_V6_vsubuhsat),    u16x1, "vsubsat.uh",    {u16x1, u16x1} },
        { IPICK(Intrinsic::hexagon_V6_vsubhsat),     i16x1, "vsubsat.h",     {i16x1, i16x1} },
        { IPICK(Intrinsic::hexagon_V6_vsubwsat),     i32x1, "vsubsat.w",     {i32x1, i32x1} },
        { IPICK(Intrinsic::hexagon_V6_vsububsat_dv), u8x2,  "vsubsat.ub.dv", {u8x2,  u8x2} },
        { IPICK(Intrinsic::hexagon_V6_vsubuhsat_dv), u16x2, "vsubsat.uh.dv", {u16x2, u16x2} },
        { IPICK(Intrinsic::hexagon_V6_vsubhsat_dv),  i16x2, "vsubsat.h.dv",  {i16x2, i16x2} },
        { IPICK(Intrinsic::hexagon_V6_vsubwsat_dv),  i32x2, "vsubsat.w.dv",  {i32x2, i32x2} },

        // Not present:
        // vaddsat.uw
        // vaddsat.b
        // vaddsat.uw.dv
        // vaddsat.b.dv
        // vsubsat.uw
        // vsubsat.b
        // vsubsat.uw.dv
        // vsubsat.b.dv


        // Averaging:
        { IPICK(Intrinsic::hexagon_V6_vavgub), u8x1,  "vavg.ub", {u8x1,  u8x1} },
        { IPICK(Intrinsic::hexagon_V6_vavguh), u16x1, "vavg.uh", {u16x1, u16x1} },
        { IPICK(Intrinsic::hexagon_V6_vavgh),  i16x1, "vavg.h",  {i16x1, i16x1} },
        { IPICK(Intrinsic::hexagon_V6_vavgw),  i32x1, "vavg.w",  {i32x1, i32x1} },

        { IPICK(Intrinsic::hexagon_V6_vavgubrnd), u8x1,  "vavgrnd.ub", {u8x1,  u8x1} },
        { IPICK(Intrinsic::hexagon_V6_vavguhrnd), u16x1, "vavgrnd.uh", {u16x1, u16x1} },
        { IPICK(Intrinsic::hexagon_V6_vavghrnd),  i16x1, "vavgrnd.h",  {i16x1, i16x1} },
        { IPICK(Intrinsic::hexagon_V6_vavgwrnd),  i32x1, "vavgrnd.w",  {i32x1, i32x1} },

        { IPICK(Intrinsic::hexagon_V6_vnavgub), u8x1,  "vnavg.ub", {u8x1,  u8x1} },
        //{ IPICK(Intrinsic::hexagon_V6_vnavguh), u16x1, "vnavg.uh", {u16x1, u16x1} },
        { IPICK(Intrinsic::hexagon_V6_vnavgh),  i16x1, "vnavg.h",  {i16x1, i16x1} },
        { IPICK(Intrinsic::hexagon_V6_vnavgw),  i32x1, "vnavg.w",  {i32x1, i32x1} },

        // Not present:
        // vavg.uw
        // vavg.b
        // vavgrnd.uw
        // vavgrnd.b
        // vnavg.uw
        // vnavg.b

        // Non-widening multiplication:
        { IPICK(Intrinsic::hexagon_V6_vmpyih),  i16x1, "vmpyi.h",  {i16x1, i16x1} },

        // Widening multiplication:
        //{ IPICK(Intrinsic::hexagon_V6_vmpyub), u16x2, "vmpy.ub", {u8x1,  u8x1} },
        //{ IPICK(Intrinsic::hexagon_V6_vmpyuh), u32x2, "vmpy.uh", {u16x1, u16x1} },
        //{ IPICK(Intrinsic::hexagon_V6_vmpyb),  i16x2, "vmpy.b",  {i8x1,  i8x1} },
        //{ IPICK(Intrinsic::hexagon_V6_vmpyh),  i32x2, "vmpy.h",  {i16x1, i16x1} },

        // Select/conditionals. Conditions are always signed integer
        // vectors (so widening sign extends).
        { IPICK(Intrinsic::hexagon_V6_vmux), i8x1,  "vmux.b",  {i8x1,  i8x1,  i8x1} },
        { IPICK(Intrinsic::hexagon_V6_vmux), i16x1, "vmux.h",  {i16x1, i16x1, i16x1} },
        { IPICK(Intrinsic::hexagon_V6_vmux), i32x1, "vmux.w",  {i32x1, i32x1, i32x1} },

        { IPICK(Intrinsic::hexagon_V6_veqb), i8x1,  "veq.b",  {i8x1,  i8x1} },
        { IPICK(Intrinsic::hexagon_V6_veqh), i16x1, "veq.h",  {i16x1, i16x1} },
        { IPICK(Intrinsic::hexagon_V6_veqw), i32x1, "veq.w",  {i32x1, i32x1} },

        { IPICK(Intrinsic::hexagon_V6_vgtub), i8x1,  "vgt.ub", {u8x1,  u8x1} },
        { IPICK(Intrinsic::hexagon_V6_vgtuh), i16x1, "vgt.uh", {u16x1, u16x1} },
        { IPICK(Intrinsic::hexagon_V6_vgtuw), i32x1, "vgt.uw", {u32x1, u32x1} },
        { IPICK(Intrinsic::hexagon_V6_vgtb),  i8x1,  "vgt.b",  {i8x1,  i8x1} },
        { IPICK(Intrinsic::hexagon_V6_vgth),  i16x1, "vgt.h",  {i16x1, i16x1} },
        { IPICK(Intrinsic::hexagon_V6_vgtw),  i32x1, "vgt.w",  {i32x1, i32x1} },

        // Min/max:
        { IPICK(Intrinsic::hexagon_V6_vmaxub), u8x1,  "vmax.ub", {u8x1,  u8x1} },
        { IPICK(Intrinsic::hexagon_V6_vmaxuh), u16x1, "vmax.uh", {u16x1, u16x1} },
        { IPICK(Intrinsic::hexagon_V6_vmaxh),  i16x1, "vmax.h",  {i16x1, i16x1} },
        { IPICK(Intrinsic::hexagon_V6_vmaxw),  i32x1, "vmax.w",  {i32x1, i32x1} },

        { IPICK(Intrinsic::hexagon_V6_vminub), u8x1,  "vmin.ub", {u8x1,  u8x1} },
        { IPICK(Intrinsic::hexagon_V6_vminuh), u16x1, "vmin.uh", {u16x1, u16x1} },
        { IPICK(Intrinsic::hexagon_V6_vminh),  i16x1, "vmin.h",  {i16x1, i16x1} },
        { IPICK(Intrinsic::hexagon_V6_vminw),  i32x1, "vmin.w",  {i32x1, i32x1} },

        // Not present:
        // vmax.uw
        // vmax.b
        // vmin.uw
        // vmin.b
    };
    for (HvxIntrinsic &i : intrinsic_wrappers) {
        define_hvx_intrinsic(i.id, i.ret_type, i.name, i.arg_types);
    }
}

llvm::Function *CodeGen_Hexagon::define_hvx_intrinsic(Intrinsic::ID id, Type ret_ty, const std::string &name,
                                                      const std::vector<Type> &arg_types) {
    internal_assert(id != Intrinsic::not_intrinsic);
    // Get the real intrinsic.
    llvm::Function *intrin = Intrinsic::getDeclaration(module.get(), id);
    return define_hvx_intrinsic(intrin, ret_ty, name, arg_types);
}

llvm::Function *CodeGen_Hexagon::define_hvx_intrinsic(llvm::Function *intrin, Type ret_ty, const std::string &name,
                                                      const std::vector<Type> &arg_types) {
    llvm::FunctionType *intrin_ty = intrin->getFunctionType();

    // Get the types of the arguments we want to pass.
    std::vector<llvm::Type *> llvm_arg_types;
    llvm_arg_types.reserve(arg_types.size());
    for (Type i : arg_types) {
        llvm_arg_types.push_back(llvm_type_of(i));
    }

    // Make a wrapper intrinsic.
    llvm::FunctionType *wrapper_ty =
        llvm::FunctionType::get(llvm_type_of(ret_ty), llvm_arg_types, false);
    llvm::Function *wrapper =
        llvm::Function::Create(wrapper_ty, llvm::GlobalValue::InternalLinkage,
                               "halide.hexagon." + name, module.get());
    llvm::BasicBlock *block = llvm::BasicBlock::Create(module->getContext(), "entry", wrapper);
    llvm::IRBuilder<> builder(module->getContext());
    builder.SetInsertPoint(block);

    std::vector<Value *> args;
    for (Value &arg : wrapper->args()) {
        args.push_back(&arg);
    }

    // Replace args with bitcasts if necessary.
    for (size_t i = 0; i < wrapper_ty->getNumParams(); i++) {
        llvm::Type *arg_ty = intrin_ty->getParamType(i);
        if (args[i]->getType() != arg_ty) {
            if (arg_ty->isVectorTy()) {
                args[i] = builder.CreateBitCast(args[i], arg_ty);
            } else {
                args[i] = builder.CreateIntCast(args[i], arg_ty, arg_types[i].is_int());
            }
        }
    }

    // Call the real intrinsic.
    Value *ret = builder.CreateCall(intrin, args);

    // Cast the result, if necessary.
    if (ret->getType() != wrapper_ty->getReturnType()) {
        ret = builder.CreateBitCast(ret, wrapper_ty->getReturnType());
    }

    builder.CreateRet(ret);

    // Always inline these wrappers.
    wrapper->addFnAttr(llvm::Attribute::AlwaysInline);

    llvm::verifyFunction(*wrapper);
    return wrapper;

}

Value *CodeGen_Hexagon::call_intrin_cast(llvm::Type *ret_ty,
                                         llvm::Function *F,
                                         std::vector<Value *> Ops) {
    llvm::FunctionType *FType = F->getFunctionType();
    internal_assert(FType->getNumParams() == Ops.size());
    for (unsigned I = 0; I < FType->getNumParams(); ++I) {
        llvm::Type *T = FType->getParamType(I);
        if (T != Ops[I]->getType()) {
            Ops[I] = builder->CreateBitCast(Ops[I], T);
        }
    }
    Value *ret = builder->CreateCall(F, Ops);
    if (ret->getType() != ret_ty) {
        ret = builder->CreateBitCast(ret, ret_ty);
    }
    return ret;
}

Value *CodeGen_Hexagon::call_intrin_cast(llvm::Type *ret_ty,
                                         llvm::Intrinsic::ID id,
                                         std::vector<Value *> Ops) {
    return call_intrin_cast(ret_ty, Intrinsic::getDeclaration(module.get(), id), Ops);
}

Value *CodeGen_Hexagon::interleave_vectors(Type type, const std::vector<Expr> &v) {
    bool B128 = target.has_feature(Halide::Target::HVX_128);
    if (v.size() == 2 && v[0].type() == v[1].type()) {
        Type v_ty = v[0].type();
        if (v_ty.bits()*v_ty.lanes() == native_vector_bits()) {
            internal_assert(v_ty.lanes()*2 == type.lanes());
            std::vector<Value *> ops = {
                codegen(v[1]),
                codegen(v[0]),
                codegen(-1*type.bytes())
            };
            return call_intrin_cast(llvm_type_of(type),
                                    IPICK(Intrinsic::hexagon_V6_vshuffvdd),
                                    ops);
        }
    }
    return CodeGen_Posix::interleave_vectors(type, v);
}

Value *CodeGen_Hexagon::slice_vector(Value *vec, int start, int size) {
    bool B128 = target.has_feature(Halide::Target::HVX_128);

    llvm::Type *vec_ty = vec->getType();
    int vec_elements = vec_ty->getVectorNumElements();
    int element_bits = vec_ty->getPrimitiveSizeInBits() / vec_elements;
    // If we're getting a native vector bits worth of data from half
    // of the argument, we might be able to use lo/hi if the start is appropriate.
    if (size*2 == vec_elements && element_bits*size == native_vector_bits()) {
        if (start == 0) {
            return call_intrin_cast(llvm::VectorType::get(vec_ty->getVectorElementType(), size),
                                    IPICK(Intrinsic::hexagon_V6_lo),
                                    {vec});
        } else if (start == vec_elements/2) {
            return call_intrin_cast(llvm::VectorType::get(vec_ty->getVectorElementType(), size),
                                    IPICK(Intrinsic::hexagon_V6_hi),
                                    {vec});
        }
    }
    return CodeGen_Posix::slice_vector(vec, start, size);
}

Value *CodeGen_Hexagon::concat_vectors(const vector<Value *> &v) {
    bool B128 = target.has_feature(Halide::Target::HVX_128);

    if (v.size() == 2 && v[0]->getType() == v[1]->getType()) {
        llvm::Type *v_ty = v[0]->getType();
        int vec_elements = v_ty->getVectorNumElements();
        int element_bits = v_ty->getPrimitiveSizeInBits() / vec_elements;
        if (vec_elements*element_bits == native_vector_bits()) {
            return call_intrin_cast(llvm::VectorType::get(v_ty->getVectorElementType(), vec_elements*2),
                                    IPICK(Intrinsic::hexagon_V6_vcombine),
                                    v);
        }
    }
    return CodeGen_Posix::concat_vectors(v);
}


namespace {
Intrinsic::ID select_intrinsic(Type type,
                               Intrinsic::ID b, Intrinsic::ID h, Intrinsic::ID w,
                               Intrinsic::ID ub, Intrinsic::ID uh, Intrinsic::ID uw) {
    switch (type.bits()) {
    case 8: return type.is_int() ? b : ub;
    case 16: return type.is_int() ? h : uh;
    case 32: return type.is_int() ? w : uw;
    }

    internal_error << "Type not handled for intrinsic.\n";
    return Intrinsic::not_intrinsic;
}

Intrinsic::ID select_intrinsic(Type type,
                               Intrinsic::ID b, Intrinsic::ID h, Intrinsic::ID w) {
    return select_intrinsic(type, b, h, w, b, h, w);
}

std::string type_suffix(Type type, bool signed_variants = true) {
    if (type.is_int() || !signed_variants) {
        switch (type.bits()) {
        case 8: return "b";
        case 16: return "h";
        case 32: return "w";
        }
    } else if (type.is_uint()) {
        switch (type.bits()) {
        case 8: return "ub";
        case 16: return "uh";
        case 32: return "uw";
        }
    }
    internal_error << "Unsupported HVX type: " << type << "\n";
    return "";
}
}  // namespace

Value *CodeGen_Hexagon::call_intrin(Type t, int intrin_lanes,
                                    llvm::Intrinsic::ID id,
                                    int ops_lanes,
                                    std::vector<Expr> ops) {
    vector<Value *> values;
    values.reserve(ops.size());
    for (Expr i : ops) {
        values.push_back(codegen(i));
    }

    return call_intrin(llvm_type_of(t), intrin_lanes, id, ops_lanes, values);
}

Value *CodeGen_Hexagon::call_intrin(Type result_type, const string &name, vector<Expr> args) {
    llvm::Function *fn = module->getFunction(name);
    internal_assert(fn) << "Function '" << name << "' not found\n";
    if (fn->getReturnType()->getVectorNumElements()*2 <= static_cast<unsigned>(result_type.lanes())) {
        // We have fewer than half as many lanes in our intrinsic as
        // we have in the call. Check to see if a double vector
        // version of this intrinsic exists.
        llvm::Function *fn2 = module->getFunction(name + ".dv");
        if (fn2) {
            fn = fn2;
        }
    }
    return call_intrin(result_type, fn->getReturnType()->getVectorNumElements(), fn->getName(), args);
}

Value *CodeGen_Hexagon::call_intrin(llvm::Type *result_type, const string &name, vector<Value *> args) {
    llvm::Function *fn = module->getFunction(name);
    internal_assert(fn) << "Function '" << name << "' not found\n";
    if (fn->getReturnType()->getVectorNumElements()*2 <= result_type->getVectorNumElements()) {
        // We have fewer than half as many lanes in our intrinsic as
        // we have in the call. Check to see if a double vector
        // version of this intrinsic exists.
        llvm::Function *fn2 = module->getFunction(name + ".dv");
        if (fn2) {
            fn = fn2;
        }
    }
    return call_intrin(result_type, fn->getReturnType()->getVectorNumElements(), fn->getName(), args);
}

Value *CodeGen_Hexagon::call_intrin(llvm::Type *t, int intrin_lanes,
                                    llvm::Intrinsic::ID id,
                                    int ops_lanes,
                                    std::vector<Value *> ops) {
    internal_assert(id != Intrinsic::not_intrinsic);
    llvm::Function *fn = Intrinsic::getDeclaration(module.get(), id);
    llvm::FunctionType *fty = fn->getFunctionType();
    internal_assert(fty->getNumParams() == ops.size());
    for (size_t i = 0; i < ops.size(); i++) {
        llvm::Type *ty = fty->getParamType(i);
        if (ty != ops[i]->getType()) {
            // We might need to cast, but the operands might have a
            // different number of lanes. Fix up vector types before
            // trying to cast.
            if (ty->isVectorTy()) {
                ty = llvm::VectorType::get(ty->getVectorElementType(),
                                           ops_lanes*ty->getVectorNumElements()/intrin_lanes);
            }
            ops[i] = builder->CreateBitCast(ops[i], ty);
        }
    }

    return CodeGen_LLVM::call_intrin(t, intrin_lanes, fn->getName(), ops);
}

string CodeGen_Hexagon::mcpu() const {
  if (target.has_feature(Halide::Target::HVX_V62))
    return "hexagonv62";
  else
    return "hexagonv60";
}

string CodeGen_Hexagon::mattrs() const {
  return "+hvx";
}

bool CodeGen_Hexagon::use_soft_float_abi() const {
  return false;
}

static bool EmittedOnce = false;
int CodeGen_Hexagon::native_vector_bits() const {
  bool DoTrace = ! EmittedOnce;
  EmittedOnce = true;
  if (DoTrace)
    debug(1) << (target.has_feature(Halide::Target::HVX_V62)
             ? "V62\n" : "V60\n");
  if (target.has_feature(Halide::Target::HVX_128)) {
    if (DoTrace) debug(1) << "128 Byte mode\n";
    return 128*8;
  } else {
    if (DoTrace) debug(1) << "64 Byte mode\n";
    return 64*8;
  }
}

int CodeGen_Hexagon::bytes_in_vector() const {
  return native_vector_bits() / 8;
}

void CodeGen_Hexagon::visit(const Add *op) {
    if (op->type.is_vector()) {
        value = call_intrin(op->type,
                            "halide.hexagon.vadd." + type_suffix(op->type, false),
                            {op->a, op->b});
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const Sub *op) {
    if (op->type.is_vector()) {
        value = call_intrin(op->type,
                            "halide.hexagon.vsub." + type_suffix(op->type, false),
                            {op->a, op->b});
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const Mul *op) {
    if (op->type.is_vector()) {
        if (op->type.code() == Type::Int && op->type.bits() == 16) {
            value = call_intrin(op->type, "halide.hexagon.vmpyi.h", {op->a, op->b});
            return;
        }

        internal_error << "Unhandled HVX multiply " << op << "\n";
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const Div *op) {
    CodeGen_Posix::visit(op);
}

void CodeGen_Hexagon::visit(const Cast *op) {
    CodeGen_Posix::visit(op);
}

void CodeGen_Hexagon::visit(const Call *op) {
    internal_assert(op->call_type == Call::Extern ||
                    op->call_type == Call::Intrinsic ||
                    op->call_type == Call::PureExtern ||
                    op->call_type == Call::PureIntrinsic)
        << "Can only codegen extern calls and intrinsics\n";

    if (starts_with(op->name, "halide.hexagon.")) {
        // I'm not sure why this is different than letting it fall
        // through to CodeGen_LLVM.
        value = call_intrin(op->type, op->name, op->args);
        return;
    }

    bool B128 = target.has_feature(Halide::Target::HVX_128);

    if (op->type.is_vector()) {
        Intrinsic::ID intrin = Intrinsic::not_intrinsic;
        int intrin_lanes = native_vector_bits() / op->type.bits();
        if (op->is_intrinsic(Call::bitwise_and)) {
            internal_assert(op->args.size() == 2);
            intrin = IPICK(Intrinsic::hexagon_V6_vand);
        } else if (op->is_intrinsic(Call::bitwise_xor)) {
            internal_assert(op->args.size() == 2);
            intrin = IPICK(Intrinsic::hexagon_V6_vxor);
        } else if (op->is_intrinsic(Call::bitwise_or)) {
            internal_assert(op->args.size() == 2);
            intrin = IPICK(Intrinsic::hexagon_V6_vor);
        } else if (op->is_intrinsic(Call::bitwise_not)) {
            internal_assert(op->args.size() == 1);
            intrin = IPICK(Intrinsic::hexagon_V6_vnot);
        } else if (op->is_intrinsic(Call::absd)) {
            internal_assert(op->args.size() == 2);
            intrin = select_intrinsic(op->args[0].type(),
                                      Intrinsic::not_intrinsic,
                                      IPICK(Intrinsic::hexagon_V6_vabsdiffh),
                                      IPICK(Intrinsic::hexagon_V6_vabsdiffw),
                                      IPICK(Intrinsic::hexagon_V6_vabsdiffub),
                                      IPICK(Intrinsic::hexagon_V6_vabsdiffuh),
                                      Intrinsic::not_intrinsic);
        }
        if (intrin != Intrinsic::not_intrinsic) {
            value = call_intrin(op->type, intrin_lanes, intrin, op->type.lanes(), op->args);
            return;
        }
    }

    if (is_interleave(op, target)) {
        value = call_intrin(op->type,
                            "halide.hexagon.interleave." + type_suffix(op->type, false),
                            {op->args[0]});
    } else if (is_deinterleave(op, target)) {
        value = call_intrin(op->type,
                            "halide.hexagon.deinterleave." + type_suffix(op->type, false),
                            {op->args[0]});
    } else if (op->is_intrinsic(Call::get_high_register)) {
        internal_assert(op->type.lanes()*2 == op->args[0].type().lanes());
        value = slice_vector(codegen(op->args[0]), op->type.lanes(), op->type.lanes());
    } else if (op->is_intrinsic(Call::get_low_register)) {
        internal_assert(op->type.lanes()*2 == op->args[0].type().lanes());
        value = slice_vector(codegen(op->args[0]), 0, op->type.lanes());
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const Broadcast *op) {
    if (is_zero(op->value)) {
        if (op->lanes * op->type.bits() <= 32) {
            // If the result is not more than 32 bits, just use scalar code.
            CodeGen_Posix::visit(op);
        } else {
            // Use vd0.
            bool B128 = target.has_feature(Halide::Target::HVX_128);
            int intrin_lanes = native_vector_bits() / op->type.bits();
            value = call_intrin(llvm_type_of(op->type), intrin_lanes,
                                IPICK(Intrinsic::hexagon_V6_vd0),
                                0, {});
        }
    } else {
        // vsplatw only splats 32 bit words, so if scalar is less, we need
        // to broadcast it to (up to) 32 bits, then use vsplat if necessary.
        int scalar_broadcast = std::min(op->lanes, 32 / op->type.bits());
        value = create_broadcast(codegen(op->value), scalar_broadcast);
        if (scalar_broadcast == op->lanes) {
            // We only needed to broadcast up to the scalar broadcast width,
            // we're done.
        } else if (op->lanes % scalar_broadcast == 0) {
            bool B128 = target.has_feature(Halide::Target::HVX_128);
            int intrin_lanes = native_vector_bits() / op->type.bits();
            value = call_intrin(llvm_type_of(op->type), intrin_lanes,
                                IPICK(Intrinsic::hexagon_V6_lvsplatw),
                                scalar_broadcast, {value});
        } else {
            // TODO: We can handle this case (the broadcast result is not
            // a multiple of 32 bits) by being clever with call_intrin.
            CodeGen_Posix::visit(op);
        }
    }
}

static bool isValidHexagonVector(Type t, int vec_bits) {
  return t.is_vector() &&
    ((t.bits() * t.lanes()) == vec_bits);
}

void CodeGen_Hexagon::visit(const Load *op) {
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  if (op->type.is_vector() && isValidHexagonVector(op->type,
                                                   native_vector_bits())) {

    bool possibly_misaligned = (might_be_misaligned.find(op->name) != might_be_misaligned.end());
    const Ramp *ramp = op->index.as<Ramp>();
    const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;
    if (ramp && stride && stride->value == 1) {
      int lanes = ramp->lanes;
      // int alignment = op->type.bytes(); // The size of a single element
      int native_vector_bytes = native_vector_bits() / 8;

      // We are loading a partial vector. if we are, default to vanilla codegen.
      if (lanes * op->type.bytes() != native_vector_bytes) {
        CodeGen_Posix::visit(op);
        return;
      }
      // At this point we are satisfied that we are loading a native vector.
      ModulusRemainder mod_rem = get_alignment_info(ramp->base);
      if (mod_rem.modulus == 1 &&
          mod_rem.remainder == 0) {
        // We know nothing about alignment. Just fall back upon vanilla codegen.
        CodeGen_Posix::visit(op);
        return;
      }
      // ModulusRemainder tells us if something can be written in the
      // form
      // (ModulusRemainder.modulus * c1) + ModulusRemainder.remainder.
      // So for us to be able to generate an aligned load, ramp->base
      // should be
      // (lanes * c1) + c2.
      if (!possibly_misaligned && !(mod_rem.modulus % lanes)) {
        if (mod_rem.remainder == 0) {
          // This is a perfectly aligned address. Vanilla codegen can deal with
          // this.
          CodeGen_Posix::visit(op);
          return;
        } else {
          Expr base = simplify(ramp->base);
          const Add *add = base.as<Add>();
          const IntImm *b = add->b.as<IntImm>();
          // We can generate a combination of two vmems (aligned) followed by
          // a valign/vlalign if The base is like
          // 1. (aligned_expr + const)
          // In the former case, for double vector mode, we will have
          // mod_rem.modulus == alignment_required and
          // mod_rem.remainder == vector_width + const
          if (!b) {
            CodeGen_Posix::visit(op);
            return;
          }
          int b_val = b->value;
          // offset_elements is an expr that tells us how many elements away we
          // are from an aligned vector;
          int offset_elements = mod_imp(b_val, lanes);
          if (offset_elements == 0) {
            CodeGen_Posix::visit(op);
            return;
          }
          // If the index is A + b, then we know that A is already aligned. We need
          // to know if b, which is an IntImm also contains an aligned vector inside.
          // For e.g. if b is 65 and lanes is 64, then we have 1 aligned vector in it.
          // and base_low should be (A + 64)
          int offset_vector = div_imp(b_val, lanes) * lanes;
          // offset_elements tells us that we are off by those many elements
          // from the vector width. We will load two vectors
          // v_low = load(add->a + offset_vector)
          // v_high = load(add->a + offset_vector + lanes)
          // Now,
          // valign (v_high, v_low, x) = vlalign(v_high, v_low, vec_length - x);
          // Since offset_elements is always between 0 and (lanes-1), we need to
          // look at the sign of b_val to create the right offset for vlalign.
          int bytes_off = b_val > 0 ? offset_elements * op->type.bytes() :
              (lanes - offset_elements)  * op->type.bytes();
          Expr base_low =  simplify(add->a + offset_vector);
          Expr base_high =  simplify(base_low + lanes);
          Expr ramp_low = Ramp::make(base_low, 1, lanes);
          Expr ramp_high = Ramp::make(base_high, 1, lanes);
          Expr load_low = Load::make(op->type, op->name, ramp_low, op->image,
                                     op->param);
          Expr load_high = Load::make(op->type, op->name, ramp_high, op->image,
                                      op->param);
          Value *vec_low = codegen(load_low);
          Value *vec_high = codegen(load_high);

          Intrinsic::ID IntrinsID = (Intrinsic::ID) 0;
          if (b_val > 0) {
            Value *Scalar;
            if (bytes_off < 7) {
              IntrinsID = IPICK(Intrinsic::hexagon_V6_valignbi);
              Expr ScalarImmExpr = IntImm::make(Int(32), bytes_off);
              Scalar = codegen(ScalarImmExpr);
            }
            else {
              IntrinsID = IPICK(Intrinsic::hexagon_V6_valignb);
              // FIXME: PDB: Is this correct? Should this require a register
              // transfer.
              Scalar = codegen(bytes_off);
            }
            value = call_intrin_cast(llvm_type_of(op->type), IntrinsID,
                                     {vec_high, vec_low, Scalar});
            return;
          } else {
            Value *Scalar;
            if (bytes_off < 7) {
              IntrinsID = IPICK(Intrinsic::hexagon_V6_vlalignbi);
              Expr ScalarImmExpr = IntImm::make(Int(32), bytes_off);
              Scalar = codegen(ScalarImmExpr);
            }
            else {
              IntrinsID = IPICK(Intrinsic::hexagon_V6_vlalignb);
              // FIXME: PDB: Is this correct? Should this require a register
              // transfer.
              Scalar = codegen(bytes_off);
            }
            value = call_intrin_cast(llvm_type_of(op->type), IntrinsID,
                                     {vec_high, vec_low, Scalar});
            return;
          }
        }
      }
    } else if (ramp && stride && stride->value == 2) {
        // Load two vectors worth and then shuffle
        Expr base_a = ramp->base, base_b = ramp->base + ramp->lanes;

        // False indicates we should take the even-numbered lanes
        // from the load, true indicates we should take the
        // odd-numbered-lanes.
        bool shifted_a = false, shifted_b = false;
        // If the base ends in an odd constant, then subtract one
        // and do a different shuffle. This helps expressions like
        // (f(2*x) + f(2*x+1) share loads
        const Add *add = ramp->base.as<Add>();
        const IntImm *offset = add ? add->b.as<IntImm>() : NULL;
        if (offset && offset->value & 1) {
          base_a -= 1;
          shifted_a = true;
          base_b -= 1;
          shifted_b = true;
        }

        // Do each load.
        Expr ramp_a = Ramp::make(base_a, 1, ramp->lanes);
        Expr ramp_b = Ramp::make(base_b, 1, ramp->lanes);
        Expr load_a = Load::make(op->type, op->name, ramp_a, op->image,
                                 op->param);
        Expr load_b = Load::make(op->type, op->name, ramp_b, op->image,
                                 op->param);
        Value *vec_a = codegen(load_a);
        Value *vec_b = codegen(load_b);

        // Shuffle together the results.
        vector<Constant *> indices(ramp->lanes);
        for (int i = 0; i < (ramp->lanes + 1)/2; i++) {
          indices[i] = ConstantInt::get(i32, i*2 + (shifted_a ? 1 : 0));
        }
        for (int i = (ramp->lanes + 1)/2; i < ramp->lanes; i++) {
          indices[i] = ConstantInt::get(i32, i*2 + (shifted_b ? 1 : 0));
        }

        debug(2) << "Loading two vectors and shuffle: \n";
        value = builder->CreateShuffleVector(vec_a, vec_b,
                                             ConstantVector::get(indices));
        if (debug::debug_level >= 2) value -> dump();
      }
  }
  if (!value)
    CodeGen_Posix::visit(op);
  return;
}

void CodeGen_Hexagon::visit(const Max *op) {
    if (op->type.is_vector()) {
        value = call_intrin(op->type,
                            "halide.hexagon.vmax." + type_suffix(op->type),
                            {op->a, op->b});
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const Min *op) {
    if (op->type.is_vector()) {
        value = call_intrin(op->type,
                            "halide.hexagon.vmin." + type_suffix(op->type),
                            {op->a, op->b});
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const Select *op) {
    if (op->type.is_vector() && op->condition.type().is_vector()) {
        // eliminate_bool_vectors has replaced all boolean vectors
        // with integer vectors of the appropriate size, and this
        // condition is of the form 'cond != 0'. We just need to grab
        // cond and use that as the operand for vmux.
        const NE *cond_ne_0 = op->condition.as<NE>();
        internal_assert(cond_ne_0);
        internal_assert(is_zero(cond_ne_0->b));
        std::vector<Value *> ops = {
            codegen(cond_ne_0->a),
            codegen(op->true_value),
            codegen(op->false_value),
        };
        value = call_intrin(llvm_type_of(op->type),
                            "halide.hexagon.vmux." + type_suffix(op->type, false),
                            ops);
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const GT *op) {
    if (op->type.is_vector()) {
        value = call_intrin(eliminated_bool_type(op->type, op->a.type()),
                            "halide.hexagon.vgt." + type_suffix(op->a.type()),
                            {op->a, op->b});
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const EQ *op) {
    if (op->type.is_vector()) {
        value = call_intrin(eliminated_bool_type(op->type, op->a.type()),
                            "halide.hexagon.veq." + type_suffix(op->a.type(), false),
                            {op->a, op->b});
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const GE *op) {
    Expr ge = Not::make(GT::make(op->b, op->a));
    ge.accept(this);
}

void CodeGen_Hexagon::visit(const LE *op) {
    Expr le = Not::make(GT::make(op->a, op->b));
    le.accept(this);
}

void CodeGen_Hexagon::visit(const LT *op) {
    Expr lt = GT::make(op->b, op->a);
    lt.accept(this);
}

void CodeGen_Hexagon::visit(const NE *op) {
    Expr eq = Not::make(EQ::make(op->a, op->b));
    eq.accept(this);
}

}}
