#include "buffer_t.h"
#include "JITCompiledModule.h"
#include "CodeGen.h"
#include "LLVM_Headers.h"
#include "Log.h"

#include <string>

namespace Halide {
namespace Internal {

using std::string;

using namespace llvm;

// Wraps an execution engine. Takes ownership of the given module and
// the memory for jit compiled code.
class JITModuleHolder {
public:
    mutable RefCount ref_count;    

    JITModuleHolder(llvm::ExecutionEngine *ee, llvm::Module *m, void (*shutdown)()) : 
        execution_engine(ee), 
        module(m), 
        context(&m->getContext()), 
        shutdown_thread_pool(shutdown) {
    }

    ~JITModuleHolder() {
        for (size_t i = 0; i < cleanup_routines.size(); i++) {
            log(2) << "Calling target specific cleanup routine at " << cleanup_routines[i] << "\n";
            (*cleanup_routines[i])();
        }

        shutdown_thread_pool();
        delete execution_engine;
        delete context;
        // No need to delete the module - deleting the execution engine should take care of that.
    }

    ExecutionEngine *execution_engine;
    Module *module;
    LLVMContext *context;
    void (*shutdown_thread_pool)();

    /** Do any target-specific module cleanup. */
    std::vector<void (*)()> cleanup_routines;
};

template<>
EXPORT RefCount &ref_count<JITModuleHolder>(const JITModuleHolder *f) {return f->ref_count;}

template<>
EXPORT void destroy<JITModuleHolder>(const JITModuleHolder *f) {delete f;}

namespace {

// Retrieve a function pointer from an llvm module, possibly by
// compiling it. Returns it by assigning to the last argument.
template<typename FP>
void hook_up_function_pointer(ExecutionEngine *ee, Module *mod, const string &name, bool must_succeed, FP *result) {

    assert(mod && ee);

    log(2) << "Retrieving " << name << " from module\n";
    llvm::Function *fn = mod->getFunction(name);
    if (!fn) {
        if (must_succeed) {
            std::cerr << "Could not find function " << name << " in module\n";
            assert(false);
        } else {
            *result = NULL;
            return;
        }
    }

    log(2) << "JIT Compiling " << name << "\n";
    void *f = ee->getPointerToFunction(fn);
    if (!f && must_succeed) {
        std::cerr << "Compiling " << name << " returned NULL\n";
        assert(false);
    }

    *result = (FP)f;

}

}


void JITCompiledModule::compile_module(CodeGen *cg, llvm::Module *m, const string &function_name) {

    // Make the execution engine
    log(2) << "Creating new execution engine\n";
    string error_string;
    
    TargetOptions options;
    options.LessPreciseFPMADOption = true;
    options.NoFramePointerElim = false;
    options.NoFramePointerElimNonLeaf = false;
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
    options.RealignStack = true;
    options.TrapFuncName = "";
    options.PositionIndependentExecutable = true;
    options.EnableSegmentedStacks = false;
    options.UseInitArray = false;
    options.SSPBufferSize = 0;
    
    EngineBuilder engine_builder(m);
    engine_builder.setTargetOptions(options);
    engine_builder.setErrorStr(&error_string);
    engine_builder.setEngineKind(EngineKind::JIT);
    #ifdef USE_MCJIT
    engine_builder.setUseMCJIT(true);        
    engine_builder.setJITMemoryManager(JITMemoryManager::CreateDefaultMemManager());
    #else
    engine_builder.setUseMCJIT(false);
    #endif
    engine_builder.setOptLevel(CodeGenOpt::Aggressive);
    engine_builder.setMCPU(cg->mcpu());    
    engine_builder.setMAttrs(vec<string>(cg->mattrs()));
    ExecutionEngine *ee = engine_builder.create();
    if (!ee) std::cerr << error_string << "\n";
    assert(ee && "Couldn't create execution engine");        
    // TODO: I don't think this is necessary, we shouldn't have any static constructors
    // ee->runStaticConstructorsDestructors(...);    

    // Do any target-specific initialization
    cg->jit_init(ee, m);

    // Retrieve function pointers from the compiled module (which also
    // triggers compilation)
    log(1) << "JIT compiling...\n";

    hook_up_function_pointer(ee, m, function_name, true, &function);

    log(1) << "JIT compiled function pointer 0x" << std::hex << (unsigned long)function << std::dec << "\n";

    hook_up_function_pointer(ee, m, function_name + "_jit_wrapper", true, &wrapped_function);
    hook_up_function_pointer(ee, m, "halide_copy_to_host", false, &copy_to_host);
    hook_up_function_pointer(ee, m, "halide_copy_to_dev", false, &copy_to_dev);
    hook_up_function_pointer(ee, m, "halide_free_dev_buffer", false, &free_dev_buffer);
    hook_up_function_pointer(ee, m, "halide_set_error_handler", true, &set_error_handler);
    hook_up_function_pointer(ee, m, "halide_set_custom_allocator", true, &set_custom_allocator);
    hook_up_function_pointer(ee, m, "halide_set_custom_do_par_for", true, &set_custom_do_par_for);
    hook_up_function_pointer(ee, m, "halide_set_custom_do_task", true, &set_custom_do_task);
    hook_up_function_pointer(ee, m, "halide_shutdown_thread_pool", true, &shutdown_thread_pool);

    ee->finalizeObject();

    // Stash the various objects that need to stay alive behind a reference-counted pointer.
    module = new JITModuleHolder(ee, m, shutdown_thread_pool);

    // Do any target-specific post-compilation module meddling
    cg->jit_finalize(ee, m, &module.ptr->cleanup_routines);

}

}
}
