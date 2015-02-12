#include <string>
#include <stdint.h>

#if __cplusplus > 199711L || _MSC_VER >= 1800
#include <mutex>
#endif

#include "buffer_t.h"
#include "JITModule.h"
#include "LLVM_Headers.h"
#include "LLVM_Runtime_Linker.h"
#include "Debug.h"
#include "LLVM_Output.h"


#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
static bool have_symbol(const char *s) {
    return GetProcAddress(GetModuleHandle(NULL), s) != NULL;
}
#else
#include <dlfcn.h>
static bool have_symbol(const char *s) {
    return dlsym(NULL, s) != NULL;
}
#endif

using std::string;

namespace Halide {
namespace Internal {

namespace {

typedef struct CUctx_st *CUcontext;

struct SharedCudaContext {
    CUctx_st *ptr;
    volatile int lock;

    // Will be created on first use by a jitted kernel that uses it
    SharedCudaContext() : ptr(0), lock(0) {
    }

    // Note that we never free the context, because static destructor
    // order is unpredictable, and we can't free the context before
    // all JITModules are freed. Users may be stashing Funcs or Images
    // in globals, and these keep JITModules around.
} cuda_ctx;

typedef struct cl_context_st *cl_context;
typedef struct cl_command_queue_st *cl_command_queue;

// A single global OpenCL context and command queue to share between
// jitted functions.
struct SharedOpenCLContext {
    cl_context context;
    cl_command_queue command_queue;
    volatile int lock;

    SharedOpenCLContext() : context(NULL), command_queue(NULL), lock(0) {
    }

    // We never free the context, for the same reason as above.
} cl_ctx;

void load_libcuda() {
    // Make sure extern cuda calls inside the module point to the
    // right things. If cuda is already linked in we should be
    // fine. If not we need to tell llvm to load it.
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
}

void load_libopencl() {
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
}

void load_opengl() {
#if defined(__linux__)
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
#elif defined(__APPLE__)
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
#else
    internal_error << "JIT support for OpenGL on anything other than linux or OS X not yet implemented\n";
#endif
}

}

using std::string;

using namespace llvm;

class JITModuleContents {
public:
    mutable RefCount ref_count;

    // Just construct a module with symbols to import into other modules.
    JITModuleContents(const std::map<std::string, JITModule::Symbol> &exports) : exports(exports),
                                                                                 execution_engine(NULL),
                                                                                 module(NULL),
                                                                                 context(NULL),
                                                                                 main_function(NULL),
                                                                                 jit_wrapper_function(NULL) {
    }

    JITModuleContents(const std::map<std::string, JITModule::Symbol> &exports,
                  llvm::ExecutionEngine *ee, llvm::Module *m, const std::vector<JITModule> &dependencies,
                  void *main_function = NULL, int (*jit_wrapper_function)(const void **) = NULL) : exports(exports),
                                                                                                   execution_engine(ee),
                                                                                                   module(m),
                                                                                                   dependencies(dependencies),
                                                                                                   context(&m->getContext()),
                                                                                                   main_function(main_function),
                                                                                                   jit_wrapper_function(jit_wrapper_function) {
    }

    ~JITModuleContents() {
        if (execution_engine != NULL) {
            execution_engine->runStaticConstructorsDestructors(true);
            delete execution_engine;
            // No need to delete the module - deleting the execution engine should take care of that.
        }
    }

    std::map<std::string, JITModule::Symbol> exports;

    ExecutionEngine *execution_engine;
    llvm::Module *module;
    std::vector<JITModule> dependencies;
    LLVMContext *context;
    void *main_function;
    int (*jit_wrapper_function)(const void **);

    std::string name;
};

template <>
EXPORT RefCount &ref_count<JITModuleContents>(const JITModuleContents *f) { return f->ref_count; }

template <>
EXPORT void destroy<JITModuleContents>(const JITModuleContents *f) { delete f; }

namespace {

// TODO: Make this thread safe.
LLVMContext llvm_context;

#ifdef __arm__
// On ARM we need to track the addresses of all the functions we
// retrieve so that we can flush the icache.
char *start, *end;
#endif

// Retrieve a function pointer from an llvm module, possibly by compiling it.
JITModule::Symbol compile_and_get_function(ExecutionEngine *ee, llvm::Module *mod, const string &name, bool optional = false) {
    internal_assert(mod && ee);

    debug(2) << "Retrieving " << name << " from module\n";
    llvm::Function *fn = mod->getFunction(name);
    if (!fn) {
        if (optional) {
            return JITModule::Symbol();
        }
        internal_error << "Could not find function " << name << " in module\n";
    }

    debug(2) << "JIT Compiling " << name << "\n";
    void *f = ee->getPointerToFunction(fn);
    if (!f) {
        if (optional) {
            return JITModule::Symbol();
        }
        internal_error << "Compiling " << name << " returned NULL\n";
    }

    JITModule::Symbol symbol(f, fn->getFunctionType());

    debug(2) << "Function " << name << " is at " << f << "\n";

#ifdef __arm__
    if (start == NULL) {
        start = (char *)f;
        end = (char *)f;
    } else {
        start = std::min(start, (char *)f);
        end = std::max(end, (char *)f + 32);
    }
#endif

    return symbol;
}

// Expand LLVM's search for symbols to include code contained in a set of JITModule.
// TODO: Does this need to be conditionalized to llvm 3.6?
class HalideJITMemoryManager : public SectionMemoryManager {
    std::vector<JITModule> modules;

public:
    HalideJITMemoryManager(const std::vector<JITModule> &modules) : modules(modules) {}

    virtual uint64_t getSymbolAddress(const std::string &name) {
        for (size_t i = 0; i < modules.size(); i++) {
            const JITModule &m = modules[i];
            std::map<std::string, JITModule::Symbol>::const_iterator iter = m.exports().find(name);
            if (iter == m.exports().end() && starts_with(name, "_")) {
                iter = m.exports().find(name.substr(1));
            }
            if (iter != m.exports().end()) {
                return (uint64_t)iter->second.address;
            }
        }
        return SectionMemoryManager::getSymbolAddress(name);
    }
};

}

JITModule::JITModule(const Module &m, const LoweredFunc &fn) {
    llvm::Module *llvm_module = output_llvm_module(m, llvm_context);
    std::vector<JITModule> shared_runtime = JITSharedRuntime::get(llvm_module, m.target());
    compile_module(llvm_module, fn.name, m.target(), shared_runtime);
}

void JITModule::compile_module(llvm::Module *m, const string &function_name, const Target &target,
                               const std::vector<JITModule> &dependencies,
                               const std::vector<std::string> &requested_exports) {

    // Make the execution engine
    debug(2) << "Creating new execution engine\n";
    debug(2) << "Target triple: " << m->getTargetTriple() << "\n";
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
#if LLVM_VERSION < 36
    // >= 3.6 there is only mcjit
    engine_builder.setUseMCJIT(true);
    //JITMemoryManager *memory_manager = JITMemoryManager::CreateDefaultMemManager();
    //engine_builder.setJITMemoryManager(memory_manager);
    HalideJITMemoryManager *memory_manager = new HalideJITMemoryManager(dependencies);
    engine_builder.setMCJITMemoryManager(memory_manager);
#else
    engine_builder.setMCJITMemoryManager(std::unique_ptr<RTDyldMemoryManager>(new HalideJITMemoryManager(dependencies)));
#endif

    engine_builder.setOptLevel(CodeGenOpt::Aggressive);
    engine_builder.setMCPU(mcpu);
    engine_builder.setMAttrs(vec<string>(mattrs));
    ExecutionEngine *ee = engine_builder.create();
    if (!ee) std::cerr << error_string << "\n";
    internal_assert(ee) << "Couldn't create execution engine\n";

#ifdef __arm__
    start = end = NULL;
#endif

    // Do any target-specific initialization
    std::vector<llvm::JITEventListener *> listeners;

    if (target.arch == Target::X86) {
        listeners.push_back(llvm::JITEventListener::createIntelJITEventListener());
    }
    // TODO: If this ever works in LLVM, this would allow profiling of JIT code with symbols with oprofile.
    //listeners.push_back(llvm::createOProfileJITEventListener());

    for (size_t i = 0; i < listeners.size(); i++) {
        ee->RegisterJITEventListener(listeners[i]);
    }

    // Add exported symbols for all dependencies.
    std::set<std::string> provided_symbols;
    for (size_t i = 0; i < dependencies.size(); i++) {
        const std::map<std::string, Symbol> &dep_exports(dependencies[i].exports());
        std::map<std::string, Symbol>::const_iterator iter;
        for (iter = dep_exports.begin(); iter != dep_exports.end(); iter++) {
            const std::string &name(iter->first);
            const Symbol &s(iter->second);
            if (provided_symbols.find(iter->first) == provided_symbols.end()) {
                if (s.llvm_type->isFunctionTy()) {
                    m->getOrInsertFunction(name, cast<FunctionType>(s.llvm_type));
                } else {
                    m->getOrInsertGlobal(name, s.llvm_type);
                }
                debug(3) << "Global value " << name << " is at address " << s.address << "\n";
                provided_symbols.insert(name);
            }
        }
    }

    // Retrieve function pointers from the compiled module (which also
    // triggers compilation)
    debug(1) << "JIT compiling...\n";

    std::map<std::string, Symbol> exports;

    void *main_fn = NULL;
    int (*wrapper_fn)(const void **) = NULL;
    if (!function_name.empty()) {
        Symbol temp;
        exports[function_name] = temp = compile_and_get_function(ee, m, function_name);
        main_fn = temp.address;
        exports[function_name + "_argv"] = temp = compile_and_get_function(ee, m, function_name + "_argv");
        wrapper_fn = reinterpret_bits<int (*)(const void **)>(temp.address);
    }

    for (size_t i = 0; i < requested_exports.size(); i++) {
        exports[requested_exports[i]] = compile_and_get_function(ee, m, requested_exports[i]);
    }

    debug(2) << "Finalizing object\n";
    ee->finalizeObject();

    // Stash the various objects that need to stay alive behind a reference-counted pointer.
    jit_module = new JITModuleContents(exports, ee, m, dependencies, main_fn, wrapper_fn);
    jit_module.ptr->name = function_name;

    // Do any target-specific post-compilation module meddling
    for (size_t i = 0; i < listeners.size(); i++) {
        ee->UnregisterJITEventListener(listeners[i]);
        delete listeners[i];
    }
    listeners.clear();

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

const std::map<std::string, JITModule::Symbol> &JITModule::exports() const {
    internal_assert(jit_module.ptr != NULL) << "JIT module is undefined\n";
    return jit_module.ptr->exports;
}

void JITModule::make_externs(const std::vector<JITModule> &deps, llvm::Module *module) {
    for (size_t i = 0; i < deps.size(); i++) {
        const std::map<std::string, Symbol> &dep_exports(deps[i].exports());
        std::map<std::string, Symbol>::const_iterator iter;
        for (iter = dep_exports.begin(); iter != dep_exports.end(); iter++) {
            const std::string &name(iter->first);
            const Symbol &s(iter->second);
            GlobalValue *gv;
            if (s.llvm_type->isFunctionTy()) {
                gv = (llvm::Function *)module->getOrInsertFunction(name, (FunctionType *)s.llvm_type);
            } else {
                gv = (GlobalValue *)module->getOrInsertGlobal(name, s.llvm_type);
            }
            gv->setLinkage(GlobalValue::ExternalWeakLinkage);
        }
    }
}

void *JITModule::main_function() const {
    if (!jit_module.defined()) {
        return NULL;
    }
    return jit_module.ptr->main_function;
}

int (*JITModule::jit_wrapper_function() const)(const void **) {
    if (!jit_module.defined()) {
        return NULL;
    }
    return (int (*)(const void **))jit_module.ptr->jit_wrapper_function;
}

void JITModule::memoization_cache_set_size(int64_t size) const {
    if (jit_module.defined()) {
        std::map<std::string, Symbol>::const_iterator f =
            exports().find("halide_memoization_cache_set_size");
        if (f != exports().end()) {
            return (reinterpret_bits<void (*)(int64_t)>(f->second.address))(size);
        }
    }
}

namespace {

JITHandlers runtime_internal_handlers;
JITHandlers default_handlers;
JITHandlers active_handlers;
int64_t default_cache_size;

void merge_handlers(JITHandlers &base, const JITHandlers &addins) {
    if (addins.custom_print) {
        base.custom_print = addins.custom_print;
    }
    if (addins.custom_malloc) {
        base.custom_malloc = addins.custom_malloc;
    }
    if (addins.custom_free) {
        base.custom_free = addins.custom_free;
    }
    if (addins.custom_do_task) {
        base.custom_do_task = addins.custom_do_task;
    }
    if (addins.custom_do_par_for) {
        base.custom_do_par_for = addins.custom_do_par_for;
    }
    if (addins.custom_error) {
        base.custom_error = addins.custom_error;
    }
    if (addins.custom_trace) {
        base.custom_trace = addins.custom_trace;
    }
}

void print_handler(void *context, const char *msg) {
    if (context) {
        JITUserContext *jit_user_context = (JITUserContext *)context;
        (*jit_user_context->handlers.custom_print)(context, msg);
    } else {
        return (*active_handlers.custom_print)(context, msg);
    }
}

void *malloc_handler(void *context, size_t x) {
    if (context) {
        JITUserContext *jit_user_context = (JITUserContext *)context;
        return (*jit_user_context->handlers.custom_malloc)(context, x);
    } else {
        return (*active_handlers.custom_malloc)(context, x);
    }
}

void free_handler(void *context, void *ptr) {
    if (context) {
        JITUserContext *jit_user_context = (JITUserContext *)context;
        (*jit_user_context->handlers.custom_free)(context, ptr);
    } else {
        (*active_handlers.custom_free)(context, ptr);
    }
}

int do_task_handler(void *context, halide_task f, int idx,
                    uint8_t *closure) {
    if (context) {
        JITUserContext *jit_user_context = (JITUserContext *)context;
        return (*jit_user_context->handlers.custom_do_task)(context, f, idx, closure);
    } else {
        return (*active_handlers.custom_do_task)(context, f, idx, closure);
    }
}

int do_par_for_handler(void *context, halide_task f,
                       int min, int size, uint8_t *closure) {
    if (context) {
        JITUserContext *jit_user_context = (JITUserContext *)context;
        return (*jit_user_context->handlers.custom_do_par_for)(context, f, min, size, closure);
    } else {
        return (*active_handlers.custom_do_par_for)(context, f, min, size, closure);
    }
}

void error_handler_handler(void *context, const char *msg) {
    if (context) {
        JITUserContext *jit_user_context = (JITUserContext *)context;
        (*jit_user_context->handlers.custom_error)(context, msg);
    } else {
        (*active_handlers.custom_error)(context, msg);
    }
}

int32_t trace_handler(void *context, const halide_trace_event *e) {
    if (context) {
        JITUserContext *jit_user_context = (JITUserContext *)context;
        return (*jit_user_context->handlers.custom_trace)(context, e);
    } else {
        return (*active_handlers.custom_trace)(context, e);
    }
}

template <typename function_t>
function_t hook_function(std::map<std::string, JITModule::Symbol> exports, const char *hook_name, function_t hook) {
    std::map<std::string, JITModule::Symbol>::const_iterator iter = exports.find(hook_name);
    internal_assert(iter != exports.end());
    function_t (*hook_setter)(function_t) =
        reinterpret_bits<function_t (*)(function_t)>(iter->second.address);
    return (*hook_setter)(hook);
}

void adjust_module_ref_count(void *arg, int32_t count) {
    JITModuleContents *module = (JITModuleContents *)arg;

    debug(2) << "Adjusting refcount for module " << module->name << " by " << count << "\n";

    if (count > 0) {
        module->ref_count.increment();
    } else {
        module->ref_count.decrement();
    }
}

#if __cplusplus > 199711L || _MSC_VER >= 1800
std::mutex shared_runtimes_mutex;
#endif

// The Halide runtime is broken up into pieces so that state can be
// shared across JIT compilations that do not use the same target
// options. At present, the split is into a MainShared module that
// contains most of the runtime except for device API specific code
// (GPU runtimes). There is one shared runtime per device API and a
// the JITModule for a Func depends on all device API modules
// specified in the target when it is JITted. (Instruction set variant
// specific code, such as math routines, is inlined into the module
// produced by compiling a Func so it can be specialized exactly for
// each target.)
enum RuntimeKind {
    MainShared,
    OpenCL,
    CUDA,
    OpenGL,
    MaxRuntimeKind
};

JITModule &shared_runtimes(RuntimeKind k) {
    static JITModule *m = NULL;
    if (!m) {
        // Note that this is never freed. On windows this would invoke
        // static destructors that use threading objects, and these
        // don't work (crash or deadlock) after main exits.
        m = new JITModule[MaxRuntimeKind];
    }
    return m[k];
}

JITModule &make_module(llvm::Module *for_module, Target target,
                       RuntimeKind runtime_kind, const std::vector<JITModule> &deps,
                       bool create) {
    if (!shared_runtimes(runtime_kind).jit_module.defined() && create) {
        // If the module has not yet been defined, we need a module to clone the target options from.
        internal_assert(for_module != NULL);

        // Ensure that JIT feature is set on target as it must be in
        // order for the right runtime components to be added.
        target.set_feature(Target::JIT);

        Target one_gpu(target);
        switch (runtime_kind) {
        case OpenCL:
            one_gpu.set_feature(Target::OpenCL);
            load_libopencl();
            break;
        case CUDA:
            one_gpu.set_feature(Target::CUDA);
            load_libcuda();
            break;
        case OpenGL:
            one_gpu.set_feature(Target::OpenGL);
            load_opengl();
            break;
        default:
            break;
        }
        llvm::Module *shared_runtime =
            get_initial_module_for_target(target, &llvm_context, true, runtime_kind != MainShared);
        clone_target_options(for_module, shared_runtime);

        std::set<std::string> halide_exports_unique;

        // Enumerate the functions.
        for (llvm::Module::const_iterator iter = shared_runtime->begin(); iter != shared_runtime->end(); iter++) {
            const llvm::Function *gv = cast<llvm::Function>(iter);
            if (gv->hasWeakLinkage() && starts_with(gv->getName(), "halide_")) {
                halide_exports_unique.insert(gv->getName());
            }
        }

        std::vector<std::string> halide_exports(halide_exports_unique.begin(), halide_exports_unique.end());

        shared_runtimes(runtime_kind).compile_module(shared_runtime, "", target, deps, halide_exports);

        if (runtime_kind == MainShared) {
            runtime_internal_handlers.custom_print =
                hook_function(shared_runtimes(MainShared).exports(), "halide_set_custom_print", print_handler);

            runtime_internal_handlers.custom_malloc =
                hook_function(shared_runtimes(MainShared).exports(), "halide_set_custom_malloc", malloc_handler);

            runtime_internal_handlers.custom_free =
                hook_function(shared_runtimes(MainShared).exports(), "halide_set_custom_free", free_handler);

            runtime_internal_handlers.custom_do_task =
                hook_function(shared_runtimes(MainShared).exports(), "halide_set_custom_do_task", do_task_handler);

            runtime_internal_handlers.custom_do_par_for =
                hook_function(shared_runtimes(MainShared).exports(), "halide_set_custom_do_par_for", do_par_for_handler);

            runtime_internal_handlers.custom_error =
                hook_function(shared_runtimes(MainShared).exports(), "halide_set_error_handler", error_handler_handler);

            runtime_internal_handlers.custom_trace =
                hook_function(shared_runtimes(MainShared).exports(), "halide_set_custom_trace", trace_handler);

            active_handlers = runtime_internal_handlers;
            merge_handlers(active_handlers, default_handlers);

            if (default_cache_size != 0) {
                shared_runtimes(MainShared).memoization_cache_set_size(default_cache_size);
            }

            shared_runtimes(runtime_kind).jit_module.ptr->name = "MainShared";
        } else {
            shared_runtimes(runtime_kind).jit_module.ptr->name = "GPU";
        }

        uint64_t arg_addr =
            shared_runtimes(runtime_kind).jit_module.ptr->execution_engine->getGlobalValueAddress("halide_jit_module_argument");

        internal_assert(arg_addr != 0);
        *((void **)arg_addr) = shared_runtimes(runtime_kind).jit_module.ptr;

        uint64_t fun_addr = shared_runtimes(runtime_kind).jit_module.ptr->execution_engine->getGlobalValueAddress("halide_jit_module_adjust_ref_count");
        internal_assert(fun_addr != 0);
        *(void (**)(void *arg, int32_t count))fun_addr = &adjust_module_ref_count;
    }
    return shared_runtimes(runtime_kind);
}

}  // anonymous namespace

/* Shared runtimes are stored as global state. The set needed is
 * determined from the target and the retrieved. If one does not exist
 * yet, it is made on the fly from the compiled in bitcode of the
 * runtime modules. As with all JITModules, the shared runtime is ref
 * counted, but a globabl keeps one ref alive until shutdown or when
 * JITSharedRuntime::release_all is called. If
 * JITSharedRuntime::release_all is called, the global state is rest
 * and any newly compiled Funcs will get a new runtime. */
std::vector<JITModule> JITSharedRuntime::get(llvm::Module *for_module, const Target &target, bool create) {
    #if __cplusplus > 199711L || _MSC_VER >= 1800
    std::lock_guard<std::mutex> lock(shared_runtimes_mutex);
    #endif

    std::vector<JITModule> result;

    JITModule m = make_module(for_module, target, MainShared, result, create);
    if (m.jit_module.defined())
        result.push_back(m);

    // Add all requested GPU modules, each only depending on the main shared runtime.
    std::vector<JITModule> gpu_modules;
    if (target.has_feature(Target::OpenCL)) {
        JITModule m = make_module(for_module, target, OpenCL, result, create);
        if (m.jit_module.defined())
            result.push_back(m);
    }
    if (target.has_feature(Target::CUDA)) {
        JITModule m = make_module(for_module, target, CUDA, result, create);
        if (m.jit_module.defined())
            result.push_back(m);
    }
    if (target.has_feature(Target::OpenGL)) {
        JITModule m = make_module(for_module, target, OpenGL, result, create);
        if (m.jit_module.defined())
            result.push_back(m);
    }

    return result;
}

// TODO: Either remove user_context argument figure out how to make
// caller provided user context work with JIT. (At present, this
// cacscaded handler calls cannot work with the right context as
// JITModule needs its context to be passed in case the called handler
// calls another callback wich is not overriden by the caller.)
void JITSharedRuntime::init_jit_user_context(JITUserContext &jit_user_context,
                                             void *user_context, const JITHandlers &handlers) {
    jit_user_context.handlers = active_handlers;
    jit_user_context.user_context = user_context;
    merge_handlers(jit_user_context.handlers, handlers);
}

void JITSharedRuntime::release_all() {
    #if __cplusplus > 199711L || _MSC_VER >= 1800
    std::lock_guard<std::mutex> lock(shared_runtimes_mutex);
    #endif

    for (int i = MaxRuntimeKind; i > 0; i--) {
        shared_runtimes((RuntimeKind)(i - 1)).jit_module = NULL;
    }
}

JITHandlers JITSharedRuntime::set_default_handlers(const JITHandlers &handlers) {
    JITHandlers result = default_handlers;
    default_handlers = handlers;
    active_handlers = runtime_internal_handlers;
    merge_handlers(active_handlers, default_handlers);
    return result;
}

void JITSharedRuntime::memoization_cache_set_size(int64_t size) {
    #if __cplusplus > 199711L || _MSC_VER >= 1800
    std::lock_guard<std::mutex> lock(shared_runtimes_mutex);
    #endif

    if (size != default_cache_size &&
        shared_runtimes(MainShared).jit_module.defined()) {
        default_cache_size = size;
        shared_runtimes(MainShared).memoization_cache_set_size(size);
    }
}


}
}
