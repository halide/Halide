#include <string>
#include <stdint.h>

#include "buffer_t.h"
#include "JITCompiledModule.h"
#include "CodeGen_LLVM.h"
#include "LLVM_Headers.h"
#include "Debug.h"

#ifdef _WIN32
#define NOMINMAX
#ifdef _WIN64
#define GPU_LIB_CC
#else
#define GPU_LIB_CC __stdcall
#endif
#include <windows.h>
static bool have_symbol(const char *s) {
    return GetProcAddress(GetModuleHandle(NULL), s) != NULL;
}
#else
#define GPU_LIB_CC
#include <dlfcn.h>
static bool have_symbol(const char *s) {
    return dlsym(NULL, s) != NULL;
}
#endif

using std::string;

namespace Halide {
namespace Internal {

namespace {

extern "C" { typedef struct CUctx_st *CUcontext; }

// A single global cuda context to share between jitted functions
int (GPU_LIB_CC *cuCtxDestroy)(CUctx_st *) = 0;

struct SharedCudaContext {
    CUctx_st *ptr;
    volatile int lock;

    // Will be created on first use by a jitted kernel that uses it
    SharedCudaContext() : ptr(0), lock(0) {
    }

    // Note that we never free the context, because static destructor
    // order is unpredictable, and we can't free the context before
    // all JITCompiledModules are freed. Users may be stashing Funcs
    // or Images in globals, and these keep JITCompiledModules around.
} cuda_ctx;

extern "C" {
    typedef struct cl_context_st *cl_context;
    typedef struct cl_command_queue_st *cl_command_queue;
}

int (GPU_LIB_CC *clReleaseContext)(cl_context);
int (GPU_LIB_CC *clReleaseCommandQueue)(cl_command_queue);

// A single global OpenCL context and command queue to share between jitted functions.
struct SharedOpenCLContext {
    cl_context context;
    cl_command_queue command_queue;
    volatile int lock;

    SharedOpenCLContext() : context(NULL), command_queue(NULL), lock(0) {
    }

    // We never free the context, for the same reason as above.
} cl_ctx;

bool lib_cuda_linked = false;

}

void jit_init(llvm::ExecutionEngine *ee, llvm::Module *module, const Target &target) {

    // Make sure extern cuda calls inside the module point to the
    // right things. If cuda is already linked in we should be
    // fine. If not we need to tell llvm to load it.
    if (target.has_feature(Target::CUDA) && !lib_cuda_linked) {
        // First check if libCuda has already been linked
        // in. If so we shouldn't need to set any mappings.
        if (have_symbol("cuInit")) {
            debug(1) << "This program was linked to cuda already\n";
        } else {
            debug(1) << "Looking for cuda shared library...\n";
            string error;
            llvm::sys::DynamicLibrary::LoadLibraryPermanently("libcuda.so", &error);
            if (!error.empty()) {
                error.clear();
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("libcuda.dylib", &error);
            }
            if (!error.empty()) {
                error.clear();
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("/Library/Frameworks/CUDA.framework/CUDA", &error);
            }
            if (!error.empty()) {
                error.clear();
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("nvcuda.dll", &error);
            }
            user_assert(error.empty()) << "Could not find libcuda.so, libcuda.dylib, or nvcuda.dll\n";
        }
        lib_cuda_linked = true;

        // Now dig out cuCtxDestroy_v2 so that we can clean up the
        // shared context at termination
        void *ptr = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol("cuCtxDestroy_v2");
        internal_assert(ptr) << "Could not find cuCtxDestroy_v2 in cuda library\n";

        cuCtxDestroy = reinterpret_bits<int (GPU_LIB_CC *)(CUctx_st *)>(ptr);

    } else if (target.has_feature(Target::OpenCL)) {
        // First check if libOpenCL has already been linked
        // in. If so we shouldn't need to set any mappings.
        if (have_symbol("clCreateContext")) {
            debug(1) << "This program was linked to OpenCL already\n";
        } else {
            debug(1) << "Looking for OpenCL shared library...\n";
            string error;
            llvm::sys::DynamicLibrary::LoadLibraryPermanently("libOpenCL.so", &error);
            if (!error.empty()) {
                error.clear();
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("/System/Library/Frameworks/OpenCL.framework/OpenCL", &error);
            }
            if (!error.empty()) {
                error.clear();
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("opencl.dll", &error); // TODO: test on Windows
            }
            user_assert(error.empty()) << "Could not find libopencl.so, OpenCL.framework, or opencl.dll\n";
        }

        // Now dig out clReleaseContext/CommandQueue so that we can clean up the
        // shared context at termination
        void *ptr = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol("clReleaseContext");
        internal_assert(ptr) << "Could not find clReleaseContext\n";

        clReleaseContext = reinterpret_bits<int (GPU_LIB_CC *)(cl_context)>(ptr);

        ptr = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol("clReleaseCommandQueue");
        internal_assert(ptr) << "Could not find clReleaseCommandQueue\n";

        clReleaseCommandQueue = reinterpret_bits<int (GPU_LIB_CC *)(cl_command_queue)>(ptr);

    } else if (target.has_feature(Target::OpenGL)) {
        if (target.os == Target::Linux) {
            if (have_symbol("glXGetCurrentContext") && have_symbol("glDeleteTextures")) {
                debug(1) << "OpenGL support code already linked in...\n";
            } else {
                debug(1) << "Looking for OpenGL support code...\n";
                string error;
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("libGL.so.1", &error);
                user_assert(error.empty()) << "Could not find libGL.so\n";
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("libX11.so", &error);
                user_assert(error.empty()) << "Could not find libX11.so\n";
            }
        } else if (target.os == Target::OSX) {
            if (have_symbol("aglCreateContext") && have_symbol("glDeleteTextures")) {
                debug(1) << "OpenGL support code already linked in...\n";
            } else {
                debug(1) << "Looking for OpenGL support code...\n";
                string error;
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("/System/Library/Frameworks/AGL.framework/AGL", &error);
                user_assert(error.empty()) << "Could not find AGL.framework\n";
                llvm::sys::DynamicLibrary::LoadLibraryPermanently("/System/Library/Frameworks/OpenGL.framework/OpenGL", &error);
                user_assert(error.empty()) << "Could not find OpenGL.framework\n";
            }
        } else {
            internal_error << "JIT support for OpenGL on anything other than linux or OS X not yet implemented\n";
        }
    }
}

void jit_finalize(llvm::ExecutionEngine *ee, llvm::Module *module, const Target &target) {
    if (target.has_feature(Target::CUDA)) {
        // Remap the cuda_ctx of PTX host modules to a shared location for all instances.
        // CUDA behaves much better when you don't initialize >2 contexts.
        llvm::Function *fn = module->getFunction("halide_set_cuda_context");
        internal_assert(fn) << "Could not find halide_set_cuda_context in module\n";
        void *f = ee->getPointerToFunction(fn);
        internal_assert(f) << "Could not find compiled form of halide_set_cuda_context in module\n";
        void (*set_cuda_context)(CUcontext *, volatile int *) =
            reinterpret_bits<void (*)(CUcontext *, volatile int *)>(f);
        set_cuda_context(&cuda_ctx.ptr, &cuda_ctx.lock);
    } else if (target.has_feature(Target::OpenCL)) {
        // Share the same cl_ctx, cl_q across all OpenCL modules.
        llvm::Function *fn = module->getFunction("halide_set_cl_context");
        internal_assert(fn) << "Could not find halide_set_cl_context in module\n";
        void *f = ee->getPointerToFunction(fn);
        internal_assert(f) << "Could not find compiled form of halide_set_cl_context in module\n";
        void (*set_cl_context)(cl_context *, cl_command_queue *, volatile int *) =
            reinterpret_bits<void (*)(cl_context *, cl_command_queue *, volatile int *)>(f);
        set_cl_context(&cl_ctx.context, &cl_ctx.command_queue, &cl_ctx.lock);
    }
}

// Wraps an execution engine. Takes ownership of the given module and
// the memory for jit compiled code.
class JITModuleHolder {
public:
    mutable RefCount ref_count;

    JITModuleHolder(llvm::ExecutionEngine *ee, llvm::Module *m, void (*stop_threads)()) :
        execution_engine(ee),
        module(m),
        context(&m->getContext()),
        shutdown_thread_pool(stop_threads) {}

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

    CodeGen_LLVM *codegen;
    llvm::ExecutionEngine *execution_engine;
    llvm::Module *module;
    llvm::LLVMContext *context;
    void (*shutdown_thread_pool)();

    /** Do any target-specific module cleanup. */
    std::vector<JITCompiledModule::CleanupRoutine> cleanup_routines;

    /** Any listeners we created. */

    /** Add a cleanup routine of the specified name from the module. */
    void add_cleanup_routine(const char *name) {
        // If the module contains the function, run it when the module dies.
        llvm::Function *fn = module->getFunction(name);
        if (fn) {
            void *f = execution_engine->getPointerToFunction(fn);
            internal_assert(f) << "Could not find compiled form of " << name << " in module.\n";
            void (*cleanup_routine)(void *) =
                reinterpret_bits<void (*)(void *)>(f);
            cleanup_routines.push_back(JITCompiledModule::CleanupRoutine(cleanup_routine, NULL));
        }
    }
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
void hook_up_function_pointer(llvm::ExecutionEngine *ee, llvm::Module *mod, const string &name,
                              bool must_succeed, FP *result) {

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

}  // namespace

JITCompiledModule::JITCompiledModule() :
    function(NULL),
    wrapped_function(NULL),
    copy_to_host(NULL),
    copy_to_dev(NULL),
    free_dev_buffer(NULL),
    set_error_handler(NULL),
    set_custom_allocator(NULL),
    set_custom_do_par_for(NULL),
    set_custom_do_task(NULL),
    set_custom_trace(NULL),
    set_custom_print(NULL),
    shutdown_thread_pool(NULL),
    memoization_cache_set_size(NULL) {
}

JITCompiledModule::JITCompiledModule(const Module &hm, const std::string &fn) :
    function(NULL),
    wrapped_function(NULL),
    copy_to_host(NULL),
    copy_to_dev(NULL),
    free_dev_buffer(NULL),
    set_error_handler(NULL),
    set_custom_allocator(NULL),
    set_custom_do_par_for(NULL),
    set_custom_do_task(NULL),
    set_custom_trace(NULL),
    set_custom_print(NULL),
    shutdown_thread_pool(NULL),
    memoization_cache_set_size(NULL) {

    llvm::Module *m = codegen_llvm(hm);

    // Make the execution engine
    debug(2) << "Creating new execution engine\n";
    string error_string;

    string mcpu;
    string mattrs;
    llvm::TargetOptions options;
    get_target_options(m, options, mcpu, mattrs);

    #if LLVM_VERSION > 35
    llvm::EngineBuilder engine_builder((std::unique_ptr<llvm::Module>(m)));
    #else
    llvm::EngineBuilder engine_builder(m);
    #endif
    engine_builder.setTargetOptions(options);
    engine_builder.setErrorStr(&error_string);
    engine_builder.setEngineKind(llvm::EngineKind::JIT);
    #ifdef USE_MCJIT
    #if LLVM_VERSION < 36
    // >= 3.6 there is only mcjit
    engine_builder.setUseMCJIT(true);
    llvm::JITMemoryManager *memory_manager = llvm::JITMemoryManager::CreateDefaultMemManager();
    engine_builder.setJITMemoryManager(memory_manager);
    #endif
    #else
    engine_builder.setUseMCJIT(false);
    #endif
    engine_builder.setOptLevel(llvm::CodeGenOpt::Aggressive);
    engine_builder.setMCPU(mcpu);
    engine_builder.setMAttrs(vec<string>(mattrs));
    llvm::ExecutionEngine *ee = engine_builder.create();
    if (!ee) std::cerr << error_string << "\n";
    internal_assert(ee) << "Couldn't create execution engine\n";

    #ifdef __arm__
    start = end = NULL;
    #endif

    // Do any target-specific initialization
    std::vector<llvm::JITEventListener *> listeners;

    if (hm.target().arch == Target::X86) {
        listeners.push_back(llvm::JITEventListener::createIntelJITEventListener());
    }
    //listeners.push_back(llvm::createOProfileJITEventListener());

    for (size_t i = 0; i < listeners.size(); i++) {
        ee->RegisterJITEventListener(listeners[i]);
    }

    jit_init(ee, m, hm.target());

    // Retrieve function pointers from the compiled module (which also
    // triggers compilation)
    debug(1) << "JIT compiling...\n";

    hook_up_function_pointer(ee, m, fn, true, &function);

    void *function_address = reinterpret_bits<void *>(function);
    debug(1) << "JIT compiled function pointer " << function_address << "\n";

    hook_up_function_pointer(ee, m, fn + "_argv", true, &wrapped_function);
    hook_up_function_pointer(ee, m, "halide_copy_to_host", false, &copy_to_host);
    hook_up_function_pointer(ee, m, "halide_copy_to_dev", false, &copy_to_dev);
    hook_up_function_pointer(ee, m, "halide_dev_free", false, &free_dev_buffer);
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
    for (size_t i = 0; i < listeners.size(); i++) {
        ee->UnregisterJITEventListener(listeners[i]);
        delete listeners[i];
    }
    listeners.clear();

    jit_finalize(ee, m, hm.target());

    // Add some more target independent cleanup routines.
    module.ptr->add_cleanup_routine("halide_memoization_cache_cleanup");
    module.ptr->add_cleanup_routine("halide_release");

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
