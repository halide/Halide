#include "CodeGen_Internal.h"
#include "CSE.h"
#include "Debug.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "LLVM_Headers.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::string;
using std::vector;

using namespace llvm;

namespace {

vector<llvm::Type*> llvm_types(const Closure& closure, llvm::StructType *buffer_t, LLVMContext &context) {
    vector<llvm::Type *> res;
    for (const auto &v : closure.vars) {
        res.push_back(llvm_type_of(&context, v.second));
    }
    for (const auto &b : closure.buffers) {
        res.push_back(llvm_type_of(&context, b.second.type)->getPointerTo());
        res.push_back(buffer_t->getPointerTo());
    }
    return res;
}

}  // namespace

StructType *build_closure_type(const Closure& closure,
                               llvm::StructType *buffer_t,
                               LLVMContext *context) {
    StructType *struct_t = StructType::create(*context, "closure_t");
    struct_t->setBody(llvm_types(closure, buffer_t, *context), false);
    return struct_t;
}

void pack_closure(llvm::StructType *type,
                  Value *dst,
                  const Closure& closure,
                  const Scope<Value *> &src,
                  llvm::StructType *buffer_t,
                  IRBuilder<> *builder) {
    // type, type of dst should be a pointer to a struct of the type returned by build_type
    int idx = 0;
    for (const auto &v : closure.vars) {
        llvm::Type *t = type->elements()[idx];
        Value *ptr = builder->CreateConstInBoundsGEP2_32(type, dst, 0, idx++);
        Value *val = src.get(v.first);
        val = builder->CreateBitCast(val, t);
        builder->CreateStore(val, ptr);
    }
    for (const auto &b : closure.buffers) {
        // For buffers we pass through base address (the symbol with
        // the same name as the buffer), and the .buffer symbol (GPU
        // code might implicitly need it).
        // FIXME: This dependence should be explicitly encoded in the IR.
        {
            llvm::Type *t = type->elements()[idx];
            Value *ptr = builder->CreateConstInBoundsGEP2_32(type, dst, 0, idx++);
            Value *val = src.get(b.first);
            val = builder->CreateBitCast(val, t);
            builder->CreateStore(val, ptr);
        }
        {
            llvm::PointerType *t = buffer_t->getPointerTo();
            Value *ptr = builder->CreateConstInBoundsGEP2_32(type, dst, 0, idx++);
            Value *val = nullptr;
            if (src.contains(b.first + ".buffer")) {
                val = src.get(b.first + ".buffer");
                val = builder->CreateBitCast(val, t);
            } else {
                val = ConstantPointerNull::get(t);
            }
            builder->CreateStore(val, ptr);
        }
    }
}

void unpack_closure(const Closure& closure,
                    Scope<Value *> &dst,
                    llvm::StructType *type,
                    Value *src,
                    IRBuilder<> *builder) {
    // type, type of src should be a pointer to a struct of the type returned by build_type
    int idx = 0;
    for (const auto &v : closure.vars) {
        Value *ptr = builder->CreateConstInBoundsGEP2_32(type, src, 0, idx++);
        LoadInst *load = builder->CreateLoad(ptr);
        dst.push(v.first, load);
        load->setName(v.first);
    }
    for (const auto &b : closure.buffers) {
        {
            Value *ptr = builder->CreateConstInBoundsGEP2_32(type, src, 0, idx++);
            LoadInst *load = builder->CreateLoad(ptr);
            dst.push(b.first, load);
            load->setName(b.first);
        }
        {
            Value *ptr = builder->CreateConstInBoundsGEP2_32(type, src, 0, idx++);
            LoadInst *load = builder->CreateLoad(ptr);
            dst.push(b.first + ".buffer", load);
            load->setName(b.first + ".buffer");
        }
    }
}

llvm::Type *llvm_type_of(LLVMContext *c, Halide::Type t) {
    if (t.lanes() == 1) {
        if (t.is_float()) {
            switch (t.bits()) {
            case 16:
                return llvm::Type::getHalfTy(*c);
            case 32:
                return llvm::Type::getFloatTy(*c);
            case 64:
                return llvm::Type::getDoubleTy(*c);
            default:
                internal_error << "There is no llvm type matching this floating-point bit width: " << t << "\n";
                return nullptr;
            }
        } else if (t.is_handle()) {
            return llvm::Type::getInt8PtrTy(*c);
        } else {
            return llvm::Type::getIntNTy(*c, t.bits());
        }
    } else {
        llvm::Type *element_type = llvm_type_of(c, t.element_of());
        return VectorType::get(element_type, t.lanes());
    }
}

// Returns true if the given function name is one of the Halide runtime
// functions that takes a user_context pointer as its first parameter.
bool function_takes_user_context(const std::string &name) {
    static const char *user_context_runtime_funcs[] = {
        "halide_buffer_copy",
        "halide_copy_to_host",
        "halide_copy_to_device",
        "halide_current_time_ns",
        "halide_debug_to_file",
        "halide_device_free",
        "halide_device_host_nop_free",
        "halide_device_free_as_destructor",
        "halide_device_and_host_free",
        "halide_device_and_host_free_as_destructor",
        "halide_device_malloc",
        "halide_device_and_host_malloc",
        "halide_device_sync",
        "halide_do_par_for",
        "halide_do_loop_task",
        "halide_do_task",
        "halide_do_async_consumer",
        "halide_error",
        "halide_free",
        "halide_malloc",
        "halide_print",
        "halide_profiler_memory_allocate",
        "halide_profiler_memory_free",
        "halide_profiler_pipeline_start",
        "halide_profiler_pipeline_end",
        "halide_profiler_stack_peak_update",
        "halide_spawn_thread",
        "halide_device_release",
        "halide_start_clock",
        "halide_trace",
        "halide_trace_helper",
        "halide_memoization_cache_lookup",
        "halide_memoization_cache_store",
        "halide_memoization_cache_release",
        "halide_cuda_run",
        "halide_opencl_run",
        "halide_opengl_run",
        "halide_openglcompute_run",
        "halide_metal_run",
        "halide_d3d12compute_run",
        "halide_msan_annotate_buffer_is_initialized_as_destructor",
        "halide_msan_annotate_buffer_is_initialized",
        "halide_msan_annotate_memory_is_initialized",
        "halide_hexagon_initialize_kernels",
        "halide_hexagon_run",
        "halide_hexagon_device_release",
        "halide_hexagon_power_hvx_on",
        "halide_hexagon_power_hvx_on_mode",
        "halide_hexagon_power_hvx_on_perf",
        "halide_hexagon_power_hvx_off",
        "halide_hexagon_power_hvx_off_as_destructor",
        "halide_qurt_hvx_lock",
        "halide_qurt_hvx_unlock",
        "halide_qurt_hvx_unlock_as_destructor",
        "halide_vtcm_malloc",
        "halide_vtcm_free",
        "halide_cuda_initialize_kernels",
        "halide_opencl_initialize_kernels",
        "halide_opengl_initialize_kernels",
        "halide_openglcompute_initialize_kernels",
        "halide_metal_initialize_kernels",
        "halide_d3d12compute_initialize_kernels",
        "halide_get_gpu_device",
        "halide_upgrade_buffer_t",
        "halide_downgrade_buffer_t",
        "halide_downgrade_buffer_t_device_fields",
        "_halide_buffer_crop",
        "_halide_buffer_retire_crop_after_extern_stage",
        "_halide_buffer_retire_crops_after_extern_stage",
    };
    const int num_funcs = sizeof(user_context_runtime_funcs) /
        sizeof(user_context_runtime_funcs[0]);
    for (int i = 0; i < num_funcs; ++i) {
        if (name == user_context_runtime_funcs[i]) {
            return true;
        }
    }
    // The error functions all take a user context
    return starts_with(name, "halide_error_");
}

bool can_allocation_fit_on_stack(int64_t size) {
    user_assert(size > 0) << "Allocation size should be a positive number\n";
    return (size <= 1024 * 16);
}

Expr lower_euclidean_div(Expr a, Expr b) {
    internal_assert(a.type() == b.type());
    // IROperator's div_round_to_zero will replace this with a / b for
    // unsigned ops, so create the intrinsic directly.
    Expr q = Call::make(a.type(), Call::div_round_to_zero, {a, b}, Call::PureIntrinsic);
    if (a.type().is_int()) {
        // Signed integer division sucks. It should be defined such
        // that it satisifies (a/b)*b + a%b = a, where 0 <= a%b < |b|,
        // i.e. Euclidean division.

        // We get rounding to work by examining the implied remainder
        // and correcting the quotient.

        /* Here's the C code that we're trying to match:
           int q = a / b;
           int r = a - q * b;
           int bs = b >> (t.bits() - 1);
           int rs = r >> (t.bits() - 1);
           return q - (rs & bs) + (rs & ~bs);
        */

        Expr r = a - q*b;
        Expr bs = b >> (a.type().bits() - 1);
        Expr rs = r >> (a.type().bits() - 1);
        q = q - (rs & bs) + (rs & ~bs);
        return common_subexpression_elimination(q);
    } else {
        return q;
    }
}

Expr lower_euclidean_mod(Expr a, Expr b) {
    internal_assert(a.type() == b.type());
    // IROperator's mod_round_to_zero will replace this with a % b for
    // unsigned ops, so create the intrinsic directly.
    Expr r = Call::make(a.type(), Call::mod_round_to_zero, {a, b}, Call::PureIntrinsic);
    if (a.type().is_int()) {
        // Match this non-overflowing C code
        /*
          T r = a % b;
          T sign_mask = (r >> (sizeof(r)*8 - 1));
          r = r + sign_mask & abs(b);
        */

        Expr sign_mask = r >> (a.type().bits()-1);
        r += sign_mask & abs(b);
        return common_subexpression_elimination(r);
    } else {
        return r;
    }
}

namespace {

// This mutator rewrites predicated loads and stores as unpredicated
// loads/stores with explicit conditions, scalarizing if necessary.
class UnpredicateLoadsStores : public IRMutator2 {
    Expr visit(const Load *op) override {
        if (is_one(op->predicate)) {
            return IRMutator2::visit(op);
        }

        Expr predicate = mutate(op->predicate);
        Expr index = mutate(op->index);
        Expr condition;

        if (const Broadcast *scalar_pred = predicate.as<Broadcast>()) {
            Expr unpredicated_load = Load::make(op->type, op->name, index, op->image, op->param,
                                                const_true(op->type.lanes()));
            return Call::make(op->type, Call::if_then_else, {scalar_pred->value, unpredicated_load, make_zero(op->type)},
                              Call::PureIntrinsic);
        } else {
            string index_name = "scalarized_load_index";
            Expr index_var = Variable::make(index.type(), index_name);
            string predicate_name = "scalarized_load_predicate";
            Expr predicate_var = Variable::make(predicate.type(), predicate_name);

            vector<Expr> lanes;
            vector<int> ramp;
            for (int i = 0; i < op->type.lanes(); i++) {
                Expr idx_i = Shuffle::make({index_var}, {i});
                Expr pred_i = Shuffle::make({predicate_var}, {i});
                Expr unpredicated_load = Load::make(op->type.element_of(), op->name, idx_i, op->image, op->param,
                                                    const_true());
                lanes.push_back(Call::make(op->type.element_of(), Call::if_then_else, {pred_i, unpredicated_load,
                                make_zero(unpredicated_load.type())}, Call::PureIntrinsic));
                ramp.push_back(i);
            }
            Expr expr = Shuffle::make(lanes, ramp);
            expr = Let::make(predicate_name, predicate, expr);
            return Let::make(index_name, index, expr);
        }
    }

    Stmt visit(const Store *op) override {
        if (is_one(op->predicate)) {
            return IRMutator2::visit(op);
        }

        Expr predicate = mutate(op->predicate);
        Expr value = mutate(op->value);
        Expr index = mutate(op->index);

        if (const Broadcast *scalar_pred = predicate.as<Broadcast>()) {
            Stmt unpredicated_store = Store::make(op->name, value, index, op->param, const_true(value.type().lanes()));
            return IfThenElse::make(scalar_pred->value, unpredicated_store);
        } else {
            string value_name = "scalarized_store_value";
            Expr value_var = Variable::make(value.type(), value_name);
            string index_name = "scalarized_store_index";
            Expr index_var = Variable::make(index.type(), index_name);
            string predicate_name = "scalarized_store_predicate";
            Expr predicate_var = Variable::make(predicate.type(), predicate_name);

            vector<Stmt> lanes;
            for (int i = 0; i < predicate.type().lanes(); i++) {
                Expr pred_i = Shuffle::make({predicate_var}, {i});
                Expr value_i = Shuffle::make({value_var}, {i});
                Expr index_i = Shuffle::make({index_var}, {i});
                Stmt lane = IfThenElse::make(pred_i, Store::make(op->name, value_i, index_i, op->param, const_true()));
                lanes.push_back(lane);
            }
            Stmt stmt = Block::make(lanes);
            stmt = LetStmt::make(predicate_name, predicate, stmt);
            stmt = LetStmt::make(value_name, value, stmt);
            return LetStmt::make(index_name, index, stmt);
       }
    }

    using IRMutator2::visit;
};

}  // namespace

Stmt unpredicate_loads_stores(Stmt s) {
    return UnpredicateLoadsStores().mutate(s);
}

bool get_md_bool(llvm::Metadata *value, bool &result) {
    if (!value) {
        return false;
    }
    llvm::ConstantAsMetadata *cam = llvm::cast<llvm::ConstantAsMetadata>(value);
    if (!cam) {
        return false;
    }
    llvm::ConstantInt *c = llvm::cast<llvm::ConstantInt>(cam->getValue());
    if (!c) {
        return false;
    }
    result = !c->isZero();
    return true;
}

bool get_md_string(llvm::Metadata *value, std::string &result) {
    if (!value) {
        result = "";
        return false;
    }
    llvm::MDString *c = llvm::dyn_cast<llvm::MDString>(value);
    if (c) {
        result = c->getString();
        return true;
    }
    return false;
}

void get_target_options(const llvm::Module &module, llvm::TargetOptions &options, std::string &mcpu, std::string &mattrs) {
    bool use_soft_float_abi = false;
    get_md_bool(module.getModuleFlag("halide_use_soft_float_abi"), use_soft_float_abi);
    get_md_string(module.getModuleFlag("halide_mcpu"), mcpu);
    get_md_string(module.getModuleFlag("halide_mattrs"), mattrs);

    bool per_instruction_fast_math_flags = false;
    get_md_bool(module.getModuleFlag("halide_per_instruction_fast_math_flags"), per_instruction_fast_math_flags);

    options = llvm::TargetOptions();
    options.AllowFPOpFusion = per_instruction_fast_math_flags ? llvm::FPOpFusion::Strict : llvm::FPOpFusion::Fast;
    options.UnsafeFPMath = !per_instruction_fast_math_flags;
    options.NoInfsFPMath = !per_instruction_fast_math_flags;
    options.NoNaNsFPMath = !per_instruction_fast_math_flags;
    options.HonorSignDependentRoundingFPMathOption = !per_instruction_fast_math_flags;
    options.NoZerosInBSS = false;
    options.GuaranteedTailCallOpt = false;
    options.StackAlignmentOverride = 0;
    options.FunctionSections = true;
    options.UseInitArray = false;
    options.FloatABIType =
        use_soft_float_abi ? llvm::FloatABI::Soft : llvm::FloatABI::Hard;
    options.RelaxELFRelocations = false;
}


void clone_target_options(const llvm::Module &from, llvm::Module &to) {
    to.setTargetTriple(from.getTargetTriple());

    llvm::LLVMContext &context = to.getContext();

    bool use_soft_float_abi = false;
    if (get_md_bool(from.getModuleFlag("halide_use_soft_float_abi"), use_soft_float_abi)) {
        to.addModuleFlag(llvm::Module::Warning, "halide_use_soft_float_abi", use_soft_float_abi ? 1 : 0);
    }

    std::string mcpu;
    if (get_md_string(from.getModuleFlag("halide_mcpu"), mcpu)) {
        to.addModuleFlag(llvm::Module::Warning, "halide_mcpu", llvm::MDString::get(context, mcpu));
    }

    std::string mattrs;
    if (get_md_string(from.getModuleFlag("halide_mattrs"), mattrs)) {
        to.addModuleFlag(llvm::Module::Warning, "halide_mattrs", llvm::MDString::get(context, mattrs));
    }
}

std::unique_ptr<llvm::TargetMachine> make_target_machine(const llvm::Module &module) {
    std::string error_string;

    const llvm::Target *llvm_target = llvm::TargetRegistry::lookupTarget(module.getTargetTriple(), error_string);
    if (!llvm_target) {
        std::cout << error_string << std::endl;
        llvm::TargetRegistry::printRegisteredTargetsForVersion(llvm::outs());
    }
    auto triple = llvm::Triple(module.getTargetTriple());
    internal_assert(llvm_target) << "Could not create LLVM target for " << triple.str() << "\n";

    llvm::TargetOptions options;
    std::string mcpu = "";
    std::string mattrs = "";
    get_target_options(module, options, mcpu, mattrs);

    return std::unique_ptr<llvm::TargetMachine>(llvm_target->createTargetMachine(module.getTargetTriple(),
                                                mcpu, mattrs,
                                                options,
                                                llvm::Reloc::PIC_,
                                                llvm::CodeModel::Small,
                                                llvm::CodeGenOpt::Aggressive));
}

void set_function_attributes_for_target(llvm::Function *fn, Target t) {
    // Turn off approximate reciprocals for division. It's too
    // inaccurate even for us.
    fn->addFnAttr("reciprocal-estimates", "none");
}

void embed_bitcode(llvm::Module *M, const string &halide_command) {
    // Save llvm.compiler.used and remote it.
    SmallVector<Constant*, 2> used_array;
    SmallPtrSet<GlobalValue*, 4> used_globals;
    llvm::Type *used_element_type = llvm::Type::getInt8Ty(M->getContext())->getPointerTo(0);
    GlobalVariable *used = collectUsedGlobalVariables(*M, used_globals, true);
    for (auto *GV : used_globals) {
        if (GV->getName() != "llvm.embedded.module" &&
            GV->getName() != "llvm.cmdline")
          used_array.push_back(
              ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV, used_element_type));
    }
    if (used) {
        used->eraseFromParent();
    }

    // Embed the bitcode for the llvm module.
    std::string data;
    Triple triple(M->getTargetTriple());
    // Create a constant that contains the bitcode.
    llvm::raw_string_ostream OS(data);
#if LLVM_VERSION >= 70
    llvm::WriteBitcodeToFile(*M, OS, /* ShouldPreserveUseListOrder */ true);
#else
    llvm::WriteBitcodeToFile(M, OS, /* ShouldPreserveUseListOrder */ true);
#endif
    ArrayRef<uint8_t> module_data =
        ArrayRef<uint8_t>((const uint8_t *)OS.str().data(), OS.str().size());

    llvm::Constant *module_constant =
        llvm::ConstantDataArray::get(M->getContext(), module_data);
    llvm::GlobalVariable *GV = new llvm::GlobalVariable(
        *M, module_constant->getType(), true, llvm::GlobalValue::PrivateLinkage,
        module_constant);
    GV->setSection((triple.getObjectFormat() == Triple::MachO) ? "__LLVM,__bitcode" : ".llvmbc");
    used_array.push_back(
        ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV, used_element_type));
    if (llvm::GlobalVariable *old =
            M->getGlobalVariable("llvm.embedded.module", true)) {
        internal_assert(old->hasOneUse()) << "llvm.embedded.module can only be used once in llvm.compiler.used";
        GV->takeName(old);
        old->eraseFromParent();
    } else {
        GV->setName("llvm.embedded.module");
    }

    // Embed command-line options.
    ArrayRef<uint8_t> command_line_data(const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(halide_command.data())),
                                        halide_command.size());
    llvm::Constant *command_line_constant =
        llvm::ConstantDataArray::get(M->getContext(), command_line_data);
    GV = new llvm::GlobalVariable(*M, command_line_constant->getType(), true,
                                  llvm::GlobalValue::PrivateLinkage,
                                  command_line_constant);

    GV->setSection((triple.getObjectFormat() == Triple::MachO) ? "__LLVM,__cmdline" : ".llvmcmd");
    used_array.push_back(
        ConstantExpr::getPointerBitCastOrAddrSpaceCast(GV, used_element_type));
    if (llvm::GlobalVariable *old =
            M->getGlobalVariable("llvm.cmdline", true)) {
        internal_assert(old->hasOneUse()) << "llvm.cmdline can only be used once in llvm.compiler.used";
        GV->takeName(old);
        old->eraseFromParent();
    } else {
        GV->setName("llvm.cmdline");
    }

    if (!used_array.empty()) {
        // Recreate llvm.compiler.used.
        ArrayType *ATy = ArrayType::get(used_element_type, used_array.size());
        auto *new_used = new GlobalVariable(
            *M, ATy, false, llvm::GlobalValue::AppendingLinkage,
            llvm::ConstantArray::get(ATy, used_array), "llvm.compiler.used");
        new_used->setSection("llvm.metadata");
    }
}

}  // namespace Internal
}  // namespace Halide
