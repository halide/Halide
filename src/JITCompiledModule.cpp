#include <string>
#include <stdint.h>

#include "buffer_t.h"
#include "JITCompiledModule.h"
#include "CodeGen.h"
#include "LLVM_Headers.h"
#include "Debug.h"

namespace Halide {
namespace Internal {

using std::string;

using namespace llvm;

// Wraps an execution engine. Takes ownership of the given module and
// the memory for jit compiled code.
class JITModuleHolder {
public:
    mutable RefCount ref_count;

    JITModuleHolder(llvm::ExecutionEngine *ee, llvm::Module *m, void (*stop_threads)()) :
        execution_engine(ee),
        module(m),
        context(&m->getContext()),
        shutdown_thread_pool(stop_threads) {
    }

    ~JITModuleHolder() {
        debug(2) << "Destroying JIT compiled module at " << this << "\n";
        for (size_t i = 0; i < cleanup_routines.size(); i++) {
            void *ptr = reinterpret_bits<void *>(cleanup_routines[i].fn);
            debug(2) << "  Calling cleanup routine [" << ptr << "]("
                     << cleanup_routines[i].context << ")\n";
            cleanup_routines[i].fn(cleanup_routines[i].context);
        }

        shutdown_thread_pool();
        execution_engine->runStaticConstructorsDestructors(true);
        delete execution_engine;
        delete context;
        // No need to delete the module - deleting the execution engine should take care of that.
    }

    ExecutionEngine *execution_engine;
    Module *module;
    LLVMContext *context;
    void (*shutdown_thread_pool)();

    /** Do any target-specific module cleanup. */
    std::vector<JITCompiledModule::CleanupRoutine> cleanup_routines;
};

template<>
EXPORT RefCount &ref_count<JITModuleHolder>(const JITModuleHolder *f) {return f->ref_count;}

template<>
EXPORT void destroy<JITModuleHolder>(const JITModuleHolder *f) {delete f;}

namespace {

#ifdef __arm__
// On ARM we need to track the addresses of all the functions we
// retrieve so that we can flush the icache.
char *start, *end;
#endif

// Retrieve a function pointer from an llvm module, possibly by
// compiling it. Returns it by assigning to the last argument.
template<typename FP>
void hook_up_function_pointer(ExecutionEngine *ee, Module *mod, const string &name, bool must_succeed, FP *result) {

    internal_assert(mod && ee);

    debug(2) << "Retrieving " << name << " from module\n";
    llvm::Function *fn = mod->getFunction(name);
    if (!fn) {
        if (must_succeed) {
            internal_error << "Could not find function " << name << " in module\n";
        } else {
            *result = NULL;
            return;
        }
    }

    debug(2) << "JIT Compiling " << name << "\n";
    void *f = ee->getPointerToFunction(fn);
    if (!f && must_succeed) {
        internal_error << "Compiling " << name << " returned NULL\n";
    }

    debug(2) << "Function " << name << " is at " << f << "\n";

    *result = reinterpret_bits<FP>(f);

    #ifdef __arm__
    if (start == NULL) {
        start = (char *)f;
        end = (char *)f;
    } else {
        start = std::min(start, (char *)f);
        end = std::max(end, (char *)f+32);
    }
    #endif
}

}

void JITCompiledModule::compile_module(CodeGen *cg, llvm::Module *m, const string &function_name) {

    // Make the execution engine
    debug(2) << "Creating new execution engine\n";
    string error_string;

    TargetOptions options;
    options.LessPreciseFPMADOption = true;
    options.NoFramePointerElim = false;
    options.AllowFPOpFusion = FPOpFusion::Fast;
    options.UnsafeFPMath = true;
    options.NoInfsFPMath = true;
    options.NoNaNsFPMath = true;
    options.HonorSignDependentRoundingFPMathOption = false;
    options.UseSoftFloat = false;
    options.FloatABIType =
        cg->use_soft_float_abi() ? FloatABI::Soft : FloatABI::Hard;
    options.NoZerosInBSS = false;
    options.GuaranteedTailCallOpt = false;
    options.DisableTailCalls = false;
    options.StackAlignmentOverride = 0;
    options.TrapFuncName = "";
    options.PositionIndependentExecutable = true;
    options.UseInitArray = false;

    #if LLVM_VERSION > 35
    EngineBuilder engine_builder((std::unique_ptr<llvm::Module>(m)));
    #else
    EngineBuilder engine_builder(m);
    #endif
    engine_builder.setTargetOptions(options);
    engine_builder.setErrorStr(&error_string);
    engine_builder.setEngineKind(EngineKind::JIT);
    #ifdef USE_MCJIT
    #if LLVM_VERSION < 36
    // >= 3.6 there is only mcjit
    engine_builder.setUseMCJIT(true);
    JITMemoryManager *memory_manager = JITMemoryManager::CreateDefaultMemManager();
    engine_builder.setJITMemoryManager(memory_manager);
    #endif
    #else
    engine_builder.setUseMCJIT(false);
    #endif
    engine_builder.setOptLevel(CodeGenOpt::Aggressive);
    engine_builder.setMCPU(cg->mcpu());
    engine_builder.setMAttrs(vec<string>(cg->mattrs()));
    ExecutionEngine *ee = engine_builder.create();
    if (!ee) std::cerr << error_string << "\n";
    internal_assert(ee) << "Couldn't create execution engine\n";

    #ifdef __arm__
    start = end = NULL;
    #endif

    // Do any target-specific initialization
    cg->jit_init(ee, m);

    // Retrieve function pointers from the compiled module (which also
    // triggers compilation)
    debug(1) << "JIT compiling...\n";

    hook_up_function_pointer(ee, m, function_name, true, &function);

    void *function_address = reinterpret_bits<void *>(function);
    debug(1) << "JIT compiled function pointer " << function_address << "\n";

    hook_up_function_pointer(ee, m, function_name + "_jit_wrapper", true, &wrapped_function);
    hook_up_function_pointer(ee, m, "halide_copy_to_host", false, &copy_to_host);
    hook_up_function_pointer(ee, m, "halide_copy_to_device", false, &copy_to_dev);
    hook_up_function_pointer(ee, m, "halide_device_free", false, &free_dev_buffer);
    hook_up_function_pointer(ee, m, "halide_set_error_handler", true, &set_error_handler);
    hook_up_function_pointer(ee, m, "halide_set_custom_allocator", true, &set_custom_allocator);
    hook_up_function_pointer(ee, m, "halide_set_custom_do_par_for", true, &set_custom_do_par_for);
    hook_up_function_pointer(ee, m, "halide_set_custom_do_task", true, &set_custom_do_task);
    hook_up_function_pointer(ee, m, "halide_set_custom_trace", true, &set_custom_trace);
    hook_up_function_pointer(ee, m, "halide_set_custom_print", true, &set_custom_print);
    hook_up_function_pointer(ee, m, "halide_shutdown_thread_pool", true, &shutdown_thread_pool);
    hook_up_function_pointer(ee, m, "halide_memoization_cache_set_size", true, &memoization_cache_set_size);

    debug(2) << "Finalizing object\n";
    ee->finalizeObject();

    // Stash the various objects that need to stay alive behind a reference-counted pointer.
    module = new JITModuleHolder(ee, m, shutdown_thread_pool);

    // Do any target-specific post-compilation module meddling
    cg->jit_finalize(ee, m, &module.ptr->cleanup_routines);

    #ifdef __arm__
    // Flush each function from the dcache so that it gets pulled into
    // the icache correctly.

    // finalizeMemory should have done the trick, but as of Aug 28
    // 2013, it doesn't work unless we also manually flush the
    // cache. Otherwise the icache's view of the code is missing the
    // relocations, which gets really confusing to debug, because
    // gdb's view of the code uses the dcache, so the disassembly
    // isn't right.
    debug(2) << "Flushing cache from " << (void *)start
             << " to " << (void *)end << "\n";
    __builtin___clear_cache(start, end);
    #endif

    // TODO: I don't think this is necessary, we shouldn't have any static constructors
    ee->runStaticConstructorsDestructors(false);

}

}
}
