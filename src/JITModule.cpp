#include <string>
#include <stdint.h>
#include <mutex>
#include <set>

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include "CodeGen_Internal.h"
#include "JITModule.h"
#include "LLVM_Headers.h"
#include "LLVM_Runtime_Linker.h"
#include "Debug.h"
#include "LLVM_Output.h"
#include "CodeGen_LLVM.h"
#include "Pipeline.h"


#if defined(_MSC_VER) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#ifdef _WIN32
#include <windows.h>
static bool have_symbol(const char *s) {
    return GetProcAddress(GetModuleHandle(nullptr), s) != nullptr;
}
#else
#include <dlfcn.h>
static bool have_symbol(const char *s) {
    return dlsym(nullptr, s) != nullptr;
}
#endif

namespace Halide {
namespace Internal {

using std::string;

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

    SharedOpenCLContext() : context(nullptr), command_queue(nullptr), lock(0) {
    }

    // We never free the context, for the same reason as above.
} cl_ctx;

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

void load_metal() {
#if defined(__APPLE__)
    if (have_symbol("MTLCreateSystemDefaultDevice")) {
        debug(1) << "Metal framework already linked in...\n";
    } else {
        debug(1) << "Looking for Metal framework...\n";
        string error;
        llvm::sys::DynamicLibrary::LoadLibraryPermanently("/System/Library/Frameworks/Metal.framework/Metal", &error);
        user_assert(error.empty()) << "Could not find Metal.framework\n";
    }
#else
    internal_error << "JIT support for Metal only implemented on OS X\n";
#endif
}

}

using namespace llvm;

class JITModuleContents {
public:
    mutable RefCount ref_count;

    // Just construct a module with symbols to import into other modules.
    JITModuleContents() : execution_engine(nullptr) {
    }

    ~JITModuleContents() {
        if (execution_engine != nullptr) {
            execution_engine->runStaticConstructorsDestructors(true);
            delete execution_engine;
        }
    }

    std::map<std::string, JITModule::Symbol> exports;
    llvm::LLVMContext context;
    ExecutionEngine *execution_engine;
    std::vector<JITModule> dependencies;
    JITModule::Symbol entrypoint;
    JITModule::Symbol argv_entrypoint;

    std::string name;
};

template <>
RefCount &ref_count<JITModuleContents>(const JITModuleContents *f) { return f->ref_count; }

template <>
void destroy<JITModuleContents>(const JITModuleContents *f) { delete f; }

namespace {

// Retrieve a function pointer from an llvm module, possibly by compiling it.
JITModule::Symbol compile_and_get_function(ExecutionEngine &ee, const string &name) {
    debug(2) << "JIT Compiling " << name << "\n";
    llvm::Function *fn = ee.FindFunctionNamed(name.c_str());
    void *f = (void *)ee.getFunctionAddress(name);
    if (!f) {
        internal_error << "Compiling " << name << " returned nullptr\n";
    }

    JITModule::Symbol symbol(f, fn->getFunctionType());

    debug(2) << "Function " << name << " is at " << f << "\n";

    return symbol;
}

// Expand LLVM's search for symbols to include code contained in a set of JITModule.
// TODO: Does this need to be conditionalized to llvm 3.6?
class HalideJITMemoryManager : public SectionMemoryManager {
    std::vector<JITModule> modules;
    std::vector<std::pair<uint8_t *, size_t>> code_pages;

public:

    HalideJITMemoryManager(const std::vector<JITModule> &modules) : modules(modules) {}

    virtual uint64_t getSymbolAddress(const std::string &name) override {
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

    virtual uint8_t *allocateCodeSection(uintptr_t size, unsigned alignment, unsigned section_id, StringRef section_name) override {
        uint8_t *result = SectionMemoryManager::allocateCodeSection(size, alignment, section_id, section_name);
        code_pages.push_back({result, size});
        return result;
    }

    void work_around_llvm_bugs() {

        for (auto p : code_pages) {
            uint8_t *start = p.first;
            uint8_t *end = p.first + p.second;

            (void)start;
            (void)end;
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
            __builtin___clear_cache((char*)start, (char*)end);
#endif

#ifndef _WIN32
            // As of November 2016, llvm doesn't always mark the right pages
            // as executable either.
            // https://llvm.org/bugs/show_bug.cgi?id=30905

            start = (uint8_t *)(((uintptr_t)start) & ~4095);
            end = (uint8_t *)(((uintptr_t)end + 4095) & ~4095);
            mprotect((void *)start, end - start, PROT_READ | PROT_EXEC);
#endif
        }
    }
};

}

JITModule::JITModule() {
    jit_module = new JITModuleContents();
}

JITModule::JITModule(const Module &m, const LoweredFunc &fn,
                     const std::vector<JITModule> &dependencies) {
    jit_module = new JITModuleContents();
    std::unique_ptr<llvm::Module> llvm_module(compile_module_to_llvm_module(m, jit_module->context));
    std::vector<JITModule> deps_with_runtime = dependencies;
    std::vector<JITModule> shared_runtime = JITSharedRuntime::get(llvm_module.get(), m.target());
    deps_with_runtime.insert(deps_with_runtime.end(), shared_runtime.begin(), shared_runtime.end());
    compile_module(std::move(llvm_module), fn.name, m.target(), deps_with_runtime);
}

void JITModule::compile_module(std::unique_ptr<llvm::Module> m, const string &function_name, const Target &target,
                               const std::vector<JITModule> &dependencies,
                               const std::vector<std::string> &requested_exports) {

    // Ensure that LLVM is initialized
    CodeGen_LLVM::initialize_llvm();

    // Make the execution engine
    debug(2) << "Creating new execution engine\n";
    debug(2) << "Target triple: " << m->getTargetTriple() << "\n";
    string error_string;

    string mcpu;
    string mattrs;
    llvm::TargetOptions options;
    get_target_options(*m, options, mcpu, mattrs);

    DataLayout initial_module_data_layout = m->getDataLayout();
    string module_name = m->getModuleIdentifier();

    llvm::EngineBuilder engine_builder((std::move(m)));
    engine_builder.setTargetOptions(options);
    engine_builder.setErrorStr(&error_string);
    engine_builder.setEngineKind(llvm::EngineKind::JIT);
    HalideJITMemoryManager *memory_manager = new HalideJITMemoryManager(dependencies);
    engine_builder.setMCJITMemoryManager(std::unique_ptr<RTDyldMemoryManager>(memory_manager));

    engine_builder.setOptLevel(CodeGenOpt::Aggressive);
    if (!mcpu.empty()) {
        engine_builder.setMCPU(mcpu);
    }
    std::vector<string> mattrs_array = {mattrs};
    engine_builder.setMAttrs(mattrs_array);

    TargetMachine *tm = engine_builder.selectTarget();
    internal_assert(tm) << error_string << "\n";
    DataLayout target_data_layout(tm->createDataLayout());
    if (initial_module_data_layout != target_data_layout) {
        internal_error << "Warning: data layout mismatch between module ("
                       << initial_module_data_layout.getStringRepresentation()
                       << ") and what the execution engine expects ("
                       << target_data_layout.getStringRepresentation() << ")\n";
    }
    ExecutionEngine *ee = engine_builder.create(tm);

    if (!ee) std::cerr << error_string << "\n";
    internal_assert(ee) << "Couldn't create execution engine\n";

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

    // Retrieve function pointers from the compiled module (which also
    // triggers compilation)
    debug(1) << "JIT compiling " << module_name << "\n";

    std::map<std::string, Symbol> exports;

    Symbol entrypoint;
    Symbol argv_entrypoint;
    if (!function_name.empty()) {
        entrypoint = compile_and_get_function(*ee, function_name);
        exports[function_name] = entrypoint;
        argv_entrypoint = compile_and_get_function(*ee, function_name + "_argv");
        exports[function_name + "_argv"] = argv_entrypoint;
    }

    for (size_t i = 0; i < requested_exports.size(); i++) {
        exports[requested_exports[i]] = compile_and_get_function(*ee, requested_exports[i]);
    }

    debug(2) << "Finalizing object\n";
    ee->finalizeObject();
    memory_manager->work_around_llvm_bugs();

    // Do any target-specific post-compilation module meddling
    for (size_t i = 0; i < listeners.size(); i++) {
        ee->UnregisterJITEventListener(listeners[i]);
        delete listeners[i];
    }
    listeners.clear();

    // TODO: I don't think this is necessary, we shouldn't have any static constructors
    ee->runStaticConstructorsDestructors(false);

    // Stash the various objects that need to stay alive behind a reference-counted pointer.
    jit_module->exports = exports;
    jit_module->execution_engine = ee;
    jit_module->dependencies = dependencies;
    jit_module->entrypoint = entrypoint;
    jit_module->argv_entrypoint = argv_entrypoint;
    jit_module->name = function_name;
}

const std::map<std::string, JITModule::Symbol> &JITModule::exports() const {
    return jit_module->exports;
}

JITModule::Symbol JITModule::find_symbol_by_name(const std::string &name) const {
    std::map<std::string, JITModule::Symbol>::iterator it = jit_module->exports.find(name);
    if (it != jit_module->exports.end()) {
        return it->second;
    }
    for (const JITModule &dep : jit_module->dependencies) {
        JITModule::Symbol s = dep.find_symbol_by_name(name);
        if (s.address) return s;
    }
    return JITModule::Symbol();
}

void *JITModule::main_function() const {
    return jit_module->entrypoint.address;
}

JITModule::Symbol JITModule::entrypoint_symbol() const {
    return jit_module->entrypoint;
}

int (*JITModule::argv_function() const)(const void **) {
    return (int (*)(const void **))jit_module->argv_entrypoint.address;
}

JITModule::Symbol JITModule::argv_entrypoint_symbol() const {
    return jit_module->argv_entrypoint;
}

static bool module_already_in_graph(const JITModuleContents *start, const JITModuleContents *target, std::set <const JITModuleContents *> &already_seen) {
    if (start == target) {
        return true;
    }
    if (already_seen.count(start) != 0) {
        return false;
    }
    already_seen.insert(start);
    for (const JITModule &dep_holder : start->dependencies) {
        const JITModuleContents *dep = dep_holder.jit_module.get();
        if (module_already_in_graph(dep, target, already_seen)) {
            return true;
        }
    }
    return false;
}

void JITModule::add_dependency(JITModule &dep) {
    std::set<const JITModuleContents *> already_seen;
    internal_assert(!module_already_in_graph(dep.jit_module.get(), jit_module.get(), already_seen)) << "JITModule::add_dependency: creating circular dependency graph.\n";
    jit_module->dependencies.push_back(dep);
}

void JITModule::add_symbol_for_export(const std::string &name, const Symbol &extern_symbol) {
    jit_module->exports[name] = extern_symbol;
}

void JITModule::add_extern_for_export(const std::string &name, const ExternCFunction &extern_c_function) {
    Symbol symbol;
    symbol.address = extern_c_function.address();

    // Struct types are uniqued on the context, but the lookup API is only available
    // on the Module, not the Context.
    llvm::Module dummy_module("ThisIsRidiculous", jit_module->context);
    llvm::Type *halide_buffer_t = dummy_module.getTypeByName("struct.halide_buffer_t");
    if (halide_buffer_t == nullptr) {
        halide_buffer_t = llvm::StructType::create(jit_module->context, "struct.halide_buffer_t");
    }
    llvm::Type *halide_buffer_t_star = llvm::PointerType::get(halide_buffer_t, 0);

    llvm::Type *ret_type;
    auto signature = extern_c_function.signature();
    if (signature.is_void_return()) {
        ret_type = llvm::Type::getVoidTy(jit_module->context);
    } else {
        ret_type = llvm_type_of(&jit_module->context, signature.ret_type());
    }

    std::vector<llvm::Type *> llvm_arg_types;
    for (const Type &t : signature.arg_types()) {
        if (t == type_of<struct halide_buffer_t *>()) {
            llvm_arg_types.push_back(halide_buffer_t_star);
        } else {
            llvm_arg_types.push_back(llvm_type_of(&jit_module->context, t));
        }
    }

    symbol.llvm_type = llvm::FunctionType::get(ret_type, llvm_arg_types, false);
    jit_module->exports[name] = symbol;
}

void JITModule::memoization_cache_set_size(int64_t size) const {
    std::map<std::string, Symbol>::const_iterator f =
        exports().find("halide_memoization_cache_set_size");
    if (f != exports().end()) {
        return (reinterpret_bits<void (*)(int64_t)>(f->second.address))(size);
    }
}

bool JITModule::compiled() const {
  return jit_module->execution_engine != nullptr;
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
    if (addins.custom_get_symbol) {
        base.custom_get_symbol = addins.custom_get_symbol;
    }
    if (addins.custom_load_library) {
        base.custom_load_library = addins.custom_load_library;
    }
    if (addins.custom_get_library_symbol) {
        base.custom_get_library_symbol = addins.custom_get_library_symbol;
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

int32_t trace_handler(void *context, const halide_trace_event_t *e) {
    if (context) {
        JITUserContext *jit_user_context = (JITUserContext *)context;
        return (*jit_user_context->handlers.custom_trace)(context, e);
    } else {
        return (*active_handlers.custom_trace)(context, e);
    }
}

void *get_symbol_handler(const char *name) {
    return (*active_handlers.custom_get_symbol)(name);
}

void *load_library_handler(const char *name) {
    return (*active_handlers.custom_load_library)(name);
}

void *get_library_symbol_handler(void *lib, const char *name) {
    return (*active_handlers.custom_get_library_symbol)(lib, name);
}

template <typename function_t>
function_t hook_function(const std::map<std::string, JITModule::Symbol> &exports, const char *hook_name, function_t hook) {
    auto iter = exports.find(hook_name);
    internal_assert(iter != exports.end()) << "Failed to find function " << hook_name << "\n";
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

std::mutex shared_runtimes_mutex;

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
    Metal,
    CUDA,
    OpenGL,
    OpenGLCompute,
    Hexagon,
    MaxRuntimeKind
};

JITModule &shared_runtimes(RuntimeKind k) {
    // We're already guarded by the shared_runtimes_mutex
    static JITModule *m = nullptr;
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
    JITModule &runtime = shared_runtimes(runtime_kind);
    if (!runtime.compiled() && create) {
        // Ensure that JIT feature is set on target as it must be in
        // order for the right runtime components to be added.
        target.set_feature(Target::JIT);

        Target one_gpu(target);
        one_gpu.set_feature(Target::OpenCL, false);
        one_gpu.set_feature(Target::Metal, false);
        one_gpu.set_feature(Target::CUDA, false);
        one_gpu.set_feature(Target::HVX_64, false);
        one_gpu.set_feature(Target::HVX_128, false);
        one_gpu.set_feature(Target::OpenGL, false);
        one_gpu.set_feature(Target::OpenGLCompute, false);
        string module_name;
        switch (runtime_kind) {
        case OpenCL:
            one_gpu.set_feature(Target::OpenCL);
            module_name = "opencl";
            break;
        case Metal:
            one_gpu.set_feature(Target::Metal);
            module_name = "metal";
            load_metal();
            break;
        case CUDA:
            one_gpu.set_feature(Target::CUDA);
            module_name = "cuda";
            break;
        case OpenGL:
            one_gpu.set_feature(Target::OpenGL);
            module_name = "opengl";
            load_opengl();
            break;
        case OpenGLCompute:
            one_gpu.set_feature(Target::OpenGLCompute);
            module_name = "openglcompute";
            load_opengl();
            break;
        case Hexagon:
            one_gpu.set_feature(Target::HVX_64);
            module_name = "hexagon";
            break;
        default:
            module_name = "shared runtime";
            break;
        }

        // This function is protected by a mutex so this is thread safe.
        std::unique_ptr<llvm::Module> module(get_initial_module_for_target(one_gpu,
            &runtime.jit_module->context, true, runtime_kind != MainShared));
        if (for_module) {
            clone_target_options(*for_module, *module);
        }
        module->setModuleIdentifier(module_name);

        std::set<std::string> halide_exports_unique;

        // Enumerate the functions.
        for (auto &f : *module) {
            // LLVM_Runtime_Linker has marked everything that should be exported as weak
            if (f.hasWeakLinkage()) {
                halide_exports_unique.insert(f.getName());
            }
        }

        std::vector<std::string> halide_exports(halide_exports_unique.begin(), halide_exports_unique.end());

        runtime.compile_module(std::move(module), "", target, deps, halide_exports);

        if (runtime_kind == MainShared) {
            runtime_internal_handlers.custom_print =
                hook_function(runtime.exports(), "halide_set_custom_print", print_handler);

            runtime_internal_handlers.custom_malloc =
                hook_function(runtime.exports(), "halide_set_custom_malloc", malloc_handler);

            runtime_internal_handlers.custom_free =
                hook_function(runtime.exports(), "halide_set_custom_free", free_handler);

            runtime_internal_handlers.custom_do_task =
                hook_function(runtime.exports(), "halide_set_custom_do_task", do_task_handler);

            runtime_internal_handlers.custom_do_par_for =
                hook_function(runtime.exports(), "halide_set_custom_do_par_for", do_par_for_handler);

            runtime_internal_handlers.custom_error =
                hook_function(runtime.exports(), "halide_set_error_handler", error_handler_handler);

            runtime_internal_handlers.custom_trace =
                hook_function(runtime.exports(), "halide_set_custom_trace", trace_handler);

            runtime_internal_handlers.custom_get_symbol =
                hook_function(shared_runtimes(MainShared).exports(), "halide_set_custom_get_symbol", get_symbol_handler);

            runtime_internal_handlers.custom_load_library =
                hook_function(shared_runtimes(MainShared).exports(), "halide_set_custom_load_library", load_library_handler);

            runtime_internal_handlers.custom_get_library_symbol =
                hook_function(shared_runtimes(MainShared).exports(), "halide_set_custom_get_library_symbol", get_library_symbol_handler);

            active_handlers = runtime_internal_handlers;
            merge_handlers(active_handlers, default_handlers);

            if (default_cache_size != 0) {
                runtime.memoization_cache_set_size(default_cache_size);
            }

            runtime.jit_module->name = "MainShared";
        } else {
            runtime.jit_module->name = "GPU";
        }

        uint64_t arg_addr =
            runtime.jit_module->execution_engine->getGlobalValueAddress("halide_jit_module_argument");

        internal_assert(arg_addr != 0);
        *((void **)arg_addr) = runtime.jit_module.get();

        uint64_t fun_addr = runtime.jit_module->execution_engine->getGlobalValueAddress("halide_jit_module_adjust_ref_count");
        internal_assert(fun_addr != 0);
        *(void (**)(void *arg, int32_t count))fun_addr = &adjust_module_ref_count;
    }
    return runtime;
}

}  // anonymous namespace

/* Shared runtimes are stored as global state. The set needed is
 * determined from the target and the retrieved. If one does not exist
 * yet, it is made on the fly from the compiled in bitcode of the
 * runtime modules. As with all JITModules, the shared runtime is ref
 * counted, but a global keeps one ref alive until shutdown or when
 * JITSharedRuntime::release_all is called. If
 * JITSharedRuntime::release_all is called, the global state is reset
 * and any newly compiled Funcs will get a new runtime. */
std::vector<JITModule> JITSharedRuntime::get(llvm::Module *for_module, const Target &target, bool create) {
    std::lock_guard<std::mutex> lock(shared_runtimes_mutex);

    std::vector<JITModule> result;

    JITModule m = make_module(for_module, target, MainShared, result, create);
    if (m.compiled()) {
        result.push_back(m);
    }

    // Add all requested GPU modules, each only depending on the main shared runtime.
    std::vector<JITModule> gpu_modules;
    if (target.has_feature(Target::OpenCL)) {
        JITModule m = make_module(for_module, target, OpenCL, result, create);
        if (m.compiled()) {
            result.push_back(m);
        }
    }
    if (target.has_feature(Target::Metal)) {
        JITModule m = make_module(for_module, target, Metal, result, create);
        if (m.compiled()) {
            result.push_back(m);
        }
    }
    if (target.has_feature(Target::CUDA)) {
        JITModule m = make_module(for_module, target, CUDA, result, create);
        if (m.compiled()) {
            result.push_back(m);
        }
    }
    if (target.has_feature(Target::OpenGL)) {
        JITModule m = make_module(for_module, target, OpenGL, result, create);
        if (m.compiled()) {
            result.push_back(m);
        }
    }
    if (target.has_feature(Target::OpenGLCompute)) {
        JITModule m = make_module(for_module, target, OpenGLCompute, result, create);
        if (m.compiled()) {
            result.push_back(m);
        }
    }
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        JITModule m = make_module(for_module, target, Hexagon, result, create);
        if (m.compiled()) {
            result.push_back(m);
        }
    }

    return result;
}

// TODO: Either remove user_context argument figure out how to make
// caller provided user context work with JIT. (At present, this
// cascaded handler calls cannot work with the right context as
// JITModule needs its context to be passed in case the called handler
// calls another callback which is not overriden by the caller.)
void JITSharedRuntime::init_jit_user_context(JITUserContext &jit_user_context,
                                             void *user_context, const JITHandlers &handlers) {
    jit_user_context.handlers = active_handlers;
    jit_user_context.user_context = user_context;
    merge_handlers(jit_user_context.handlers, handlers);
}

void JITSharedRuntime::release_all() {
    std::lock_guard<std::mutex> lock(shared_runtimes_mutex);

    for (int i = MaxRuntimeKind; i > 0; i--) {
        shared_runtimes((RuntimeKind)(i - 1)) = JITModule();
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
    std::lock_guard<std::mutex> lock(shared_runtimes_mutex);

    if (size != default_cache_size) {
        default_cache_size = size;
        shared_runtimes(MainShared).memoization_cache_set_size(size);
    }
}

}
}
