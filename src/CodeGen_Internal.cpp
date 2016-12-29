#include "CodeGen_Internal.h"
#include "IROperator.h"
#include "CSE.h"
#include "Debug.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::vector;
using std::pair;

using namespace llvm;

namespace {

vector<llvm::Type*> llvm_types(const Closure& closure, llvm::StructType *buffer_t, LLVMContext &context) {
    vector<llvm::Type *> res;
    for (const pair<string, Type> &i : closure.vars) {
        res.push_back(llvm_type_of(&context, i.second));
    }
    for (const pair<string, Closure::BufferRef> &i : closure.buffers) {
        res.push_back(llvm_type_of(&context, i.second.type)->getPointerTo());
        res.push_back(buffer_t->getPointerTo());
    }
    return res;
}

}  // namespace

StructType *build_closure_type(const Closure& closure, llvm::StructType *buffer_t, LLVMContext *context) {
    StructType *struct_t = StructType::create(*context, "closure_t");
    struct_t->setBody(llvm_types(closure, buffer_t, *context), false);
    return struct_t;
}

void pack_closure(llvm::Type *type,
                  Value *dst,
                  const Closure& closure,
                  const Scope<Value *> &src,
                  llvm::StructType *buffer_t,
                  IRBuilder<> *builder) {
    // type, type of dst should be a pointer to a struct of the type returned by build_type
    int idx = 0;
    LLVMContext &context = builder->getContext();
    vector<string> nm = closure.names();
    vector<llvm::Type*> ty = llvm_types(closure, buffer_t, context);
    for (size_t i = 0; i < nm.size(); i++) {
        Value *ptr = builder->CreateConstInBoundsGEP2_32(type, dst, 0, idx);
        Value *val;
        if (!ends_with(nm[i], ".buffer") || src.contains(nm[i])) {
            val = src.get(nm[i]);
            if (val->getType() != ty[i]) {
                val = builder->CreateBitCast(val, ty[i]);
            }
        } else {
            // Skip over buffers not in the symbol table. They must not be needed.
            val = ConstantPointerNull::get(buffer_t->getPointerTo());
        }
        builder->CreateStore(val, ptr);
        idx++;
    }
}

void unpack_closure(const Closure& closure,
                    Scope<Value *> &dst,
                    llvm::Type *type,
                    Value *src,
                    IRBuilder<> *builder) {
    // type, type of src should be a pointer to a struct of the type returned by build_type
    int idx = 0;
    vector<string> nm = closure.names();
    for (size_t i = 0; i < nm.size(); i++) {
        Value *ptr = builder->CreateConstInBoundsGEP2_32(type, src, 0, idx++);
        LoadInst *load = builder->CreateLoad(ptr);
        dst.push(nm[i], load);
        load->setName(nm[i]);
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
        "halide_do_task",
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
        "halide_memoization_cache_lookup",
        "halide_memoization_cache_store",
        "halide_memoization_cache_release",
        "halide_cuda_run",
        "halide_opencl_run",
        "halide_opengl_run",
        "halide_openglcompute_run",
        "halide_metal_run",
        "halide_msan_annotate_buffer_is_initialized_as_destructor",
        "halide_msan_annotate_buffer_is_initialized",
        "halide_msan_annotate_memory_is_initialized",
        "halide_hexagon_initialize_kernels",
        "halide_hexagon_run",
        "halide_hexagon_device_release",
        "halide_hexagon_host_get_symbol",
        "halide_hexagon_power_hvx_on",
        "halide_hexagon_power_hvx_on_mode",
        "halide_hexagon_power_hvx_on_perf",
        "halide_hexagon_power_hvx_off",
        "halide_hexagon_power_hvx_off_as_destructor",
        "halide_qurt_hvx_lock",
        "halide_qurt_hvx_unlock",
        "halide_qurt_hvx_unlock_as_destructor",
        "halide_cuda_initialize_kernels",
        "halide_opencl_initialize_kernels",
        "halide_opengl_initialize_kernels",
        "halide_openglcompute_initialize_kernels",
        "halide_metal_initialize_kernels",
        "halide_get_gpu_device",
        "halide_upgrade_buffer_t",
        "halide_downgrade_buffer_t",
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

    options = llvm::TargetOptions();
    options.LessPreciseFPMADOption = true;
    options.AllowFPOpFusion = llvm::FPOpFusion::Fast;
    options.UnsafeFPMath = true;

    #if LLVM_VERSION < 40
    // Turn off approximate reciprocals for division. It's too
    // inaccurate even for us. In LLVM 4.0+ this moved to be a
    // function attribute.
    options.Reciprocals.setDefaults("all", false, 0);
    #endif

    options.NoInfsFPMath = true;
    options.NoNaNsFPMath = true;
    options.HonorSignDependentRoundingFPMathOption = false;
    options.NoZerosInBSS = false;
    options.GuaranteedTailCallOpt = false;
    options.StackAlignmentOverride = 0;
    options.FunctionSections = true;
    options.UseInitArray = false;
    options.FloatABIType =
        use_soft_float_abi ? llvm::FloatABI::Soft : llvm::FloatABI::Hard;
    #if LLVM_VERSION >= 39
    // Not supported by older linkers
    options.RelaxELFRelocations = false;
    #endif
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

    const llvm::Target *target = llvm::TargetRegistry::lookupTarget(module.getTargetTriple(), error_string);
    if (!target) {
        std::cout << error_string << std::endl;
        llvm::TargetRegistry::printRegisteredTargetsForVersion();
    }
    internal_assert(target) << "Could not create target for " << module.getTargetTriple() << "\n";

    llvm::TargetOptions options;
    std::string mcpu = "";
    std::string mattrs = "";
    get_target_options(module, options, mcpu, mattrs);

    return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(module.getTargetTriple(),
                                                mcpu, mattrs,
                                                options,
                                                llvm::Reloc::PIC_,
                                                llvm::CodeModel::Default,
                                                llvm::CodeGenOpt::Aggressive));
}

void set_function_attributes_for_target(llvm::Function *fn, Target t) {
    #if LLVM_VERSION >= 40
    // Turn off approximate reciprocals for division. It's too
    // inaccurate even for us.
    fn->addFnAttr("reciprocal-estimates", "none");
    #endif
}

}
}
