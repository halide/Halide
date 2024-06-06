#include <cstdint>
#include <mutex>
#include <set>
#include <string>

#ifdef _WIN32
#ifdef _MSC_VER
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/mman.h>
#endif

#include "CodeGen_Internal.h"
#include "CodeGen_LLVM.h"
#include "Debug.h"
#include "JITModule.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "LLVM_Runtime_Linker.h"
#include "Pipeline.h"
#include "WasmExecutor.h"

namespace Halide {
namespace Internal {

using std::string;

#if defined(__GNUC__) && defined(__i386__)
extern "C" unsigned long __udivdi3(unsigned long a, unsigned long b);
#endif

#ifdef _WIN32
void *get_symbol_address(const char *s) {
    return (void *)GetProcAddress(GetModuleHandle(nullptr), s);
}
#else
void *get_symbol_address(const char *s) {
    // Mac OS 10.11 fails to return a symbol address if nullptr or RTLD_DEFAULT
    // is passed to dlsym. This seems to work.
    void *handle = dlopen(nullptr, RTLD_LAZY);
    void *result = dlsym(handle, s);
    dlclose(handle);
    return result;
}
#endif

namespace {

bool have_symbol(const char *s) {
    return get_symbol_address(s) != nullptr;
}

typedef struct CUctx_st *CUcontext;

typedef struct cl_context_st *cl_context;
typedef struct cl_command_queue_st *cl_command_queue;

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

void load_vulkan() {
    if (have_symbol("vkGetInstanceProcAddr")) {
        debug(1) << "Vulkan support code already linked in...\n";
    } else {
        debug(1) << "Looking for Vulkan support code...\n";
        string error;
#if defined(__linux__)
        llvm::sys::DynamicLibrary::LoadLibraryPermanently("libvulkan.so.1", &error);
        user_assert(error.empty()) << "Could not find libvulkan.so.1\n";
#elif defined(__APPLE__)
        llvm::sys::DynamicLibrary::LoadLibraryPermanently("libvulkan.1.dylib", &error);
        user_assert(error.empty()) << "Could not find libvulkan.1.dylib\n";
#elif defined(_WIN32)
        llvm::sys::DynamicLibrary::LoadLibraryPermanently("vulkan-1.dll", &error);
        user_assert(error.empty()) << "Could not find vulkan-1.dll\n";
#else
        internal_error << "JIT support for Vulkan only available on Linux, OS X and Windows!\n";
#endif
    }
}

void load_webgpu() {
    debug(1) << "Looking for a native WebGPU implementation...\n";

    const auto try_load = [](const char *libname) -> string {
        debug(1) << "Trying " << libname << "... ";
        string error;
        llvm::sys::DynamicLibrary::LoadLibraryPermanently(libname, &error);
        debug(1) << (error.empty() ? "found!\n" : "not found.\n");
        return error;
    };

    string error;

    auto env_libname = get_env_variable("HL_WEBGPU_NATIVE_LIB");
    if (!env_libname.empty()) {
        error = try_load(env_libname.c_str());
    }
    if (!error.empty()) {
        const char *libnames[] = {
            // Dawn (Chromium).
            "libwebgpu_dawn.so",
            "libwebgpu_dawn.dylib",
            "webgpu_dawn.dll",

            // wgpu (Firefox).
            "libwgpu.so",
            "libwgpu.dylib",
            "wgpu.dll",
        };

        for (const char *libname : libnames) {
            error = try_load(libname);
            if (error.empty()) {
                break;
            }
        }
    }
    user_assert(error.empty()) << "Could not find a native WebGPU library: " << error << "\n"
                               << "(Try setting the env var HL_WEBGPU_NATIVE_LIB to an explicit path to fix this.)\n";
}

}  // namespace

using namespace llvm;

class JITModuleContents {
public:
    mutable RefCount ref_count;

    // Just construct a module with symbols to import into other modules.
    JITModuleContents() = default;

    ~JITModuleContents() {
        if (JIT != nullptr) {
            auto err = dtorRunner->run();
            internal_assert(!err) << llvm::toString(std::move(err)) << "\n";
        }
    }

    std::map<std::string, JITModule::Symbol> exports;
    std::unique_ptr<llvm::LLVMContext> context = std::make_unique<llvm::LLVMContext>();
    std::unique_ptr<llvm::orc::LLJIT> JIT = nullptr;
    std::unique_ptr<llvm::orc::CtorDtorRunner> dtorRunner = nullptr;
    std::vector<JITModule> dependencies;
    JITModule::Symbol entrypoint;
    JITModule::Symbol argv_entrypoint;

    std::string name;
};

template<>
RefCount &ref_count<JITModuleContents>(const JITModuleContents *f) noexcept {
    return f->ref_count;
}

template<>
void destroy<JITModuleContents>(const JITModuleContents *f) {
    delete f;
}

namespace {

// Retrieve a function pointer from an llvm module, possibly by compiling it.
JITModule::Symbol compile_and_get_function(llvm::orc::LLJIT &JIT, const string &name) {
    debug(2) << "JIT Compiling " << name << "\n";

    auto addr = JIT.lookup(name);
    internal_assert(addr) << llvm::toString(addr.takeError()) << "\n";

    void *f = (void *)addr->getValue();
    if (!f) {
        internal_error << "Compiling " << name << " returned nullptr\n";
    }

    JITModule::Symbol symbol(f);

    debug(2) << "Function " << name << " is at " << f << "\n";

    return symbol;
}

// Expand LLVM's search for symbols to include code contained in a set of JITModule.
class HalideJITMemoryManager : public SectionMemoryManager {
    std::vector<JITModule> modules;
    std::vector<std::pair<uint8_t *, size_t>> code_pages;

public:
    HalideJITMemoryManager(const std::vector<JITModule> &modules)
        : modules(modules) {
    }

    uint64_t getSymbolAddress(const std::string &name) override {
        for (const auto &module : modules) {
            std::map<std::string, JITModule::Symbol>::const_iterator iter = module.exports().find(name);
            if (iter == module.exports().end() && starts_with(name, "_")) {
                iter = module.exports().find(name.substr(1));
            }
            if (iter != module.exports().end()) {
                return (uint64_t)iter->second.address;
            }
        }
        uint64_t result = SectionMemoryManager::getSymbolAddress(name);
#if defined(__GNUC__) && defined(__i386__)
        // This is a workaround for an odd corner case (cross-compiling + testing
        // Python bindings x86-32 on an x86-64 system): __udivdi3 is a helper function
        // that GCC uses to do u64/u64 division on 32-bit systems; it's usually included
        // by the linker on these systems as needed. When we JIT, LLVM will include references
        // to this call; MCJIT fixes up these references by doing (roughly) dlopen(NULL)
        // to look up the symbol. For normal JIT tests, this works fine, as dlopen(NULL)
        // finds the test executable, which has the right lookups to locate it inside libHalide.so.
        // If, however, we are running a JIT-via-Python test, dlopen(NULL) returns the
        // CPython executable... which apparently *doesn't* include this as an exported
        // function, so the lookup fails and crashiness ensues. So our workaround here is
        // a bit icky, but expedient: check for this name if we can't find it elsewhere,
        // and if so, return the one we know should be present. (Obviously, if other runtime
        // helper functions of this sort crop up in the future, this should be expanded
        // into a "builtins map".)
        if (result == 0 && name == "__udivdi3") {
            result = (uint64_t)&__udivdi3;
        }
#endif
        internal_assert(result != 0)
            << "HalideJITMemoryManager: unable to find address for " << name << "\n";
        return result;
    }

    uint8_t *allocateCodeSection(uintptr_t size, unsigned alignment, unsigned section_id, StringRef section_name) override {
        uint8_t *result = SectionMemoryManager::allocateCodeSection(size, alignment, section_id, section_name);
        code_pages.emplace_back(result, size);
        return result;
    }
};

}  // namespace

JITModule::JITModule() {
    jit_module = new JITModuleContents();
}

JITModule::JITModule(const Module &m, const LoweredFunc &fn,
                     const std::vector<JITModule> &dependencies) {
    jit_module = new JITModuleContents();
    std::unique_ptr<llvm::Module> llvm_module(compile_module_to_llvm_module(m, *jit_module->context));
    std::vector<JITModule> deps_with_runtime = dependencies;
    std::vector<JITModule> shared_runtime = JITSharedRuntime::get(llvm_module.get(), m.target());
    deps_with_runtime.insert(deps_with_runtime.end(), shared_runtime.begin(), shared_runtime.end());
    compile_module(std::move(llvm_module), fn.name, m.target(), deps_with_runtime);
    // If -time-passes is in HL_LLVM_ARGS, this will print llvm passes time statstics otherwise its no-op.
    llvm::reportAndResetTimings();
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

    llvm::for_each(*m, set_function_attributes_from_halide_target_options);

    llvm::TargetOptions options;
    get_target_options(*m, options);

    DataLayout initial_module_data_layout = m->getDataLayout();
    string module_name = m->getModuleIdentifier();

    // Build TargetMachine
    llvm::orc::JITTargetMachineBuilder tm_builder(llvm::Triple(m->getTargetTriple()));
    tm_builder.setOptions(options);
#if LLVM_VERSION >= 180
    tm_builder.setCodeGenOptLevel(CodeGenOptLevel::Aggressive);
#else
    tm_builder.setCodeGenOptLevel(CodeGenOpt::Aggressive);
#endif
    if (target.arch == Target::Arch::RISCV) {
        tm_builder.setCodeModel(llvm::CodeModel::Medium);
    }

    auto tm = tm_builder.createTargetMachine();
    internal_assert(tm) << llvm::toString(tm.takeError()) << "\n";

    DataLayout target_data_layout(tm.get()->createDataLayout());
    if (initial_module_data_layout != target_data_layout) {
        internal_error << "Warning: data layout mismatch between module ("
                       << initial_module_data_layout.getStringRepresentation()
                       << ") and what the execution engine expects ("
                       << target_data_layout.getStringRepresentation() << ")\n";
    }

    // Create LLJIT
    const auto compilerBuilder = [&](const llvm::orc::JITTargetMachineBuilder & /*jtmb*/)
        -> llvm::Expected<std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler>> {
        return std::make_unique<llvm::orc::TMOwningSimpleCompiler>(std::move(*tm));
    };

    llvm::orc::LLJITBuilderState::ObjectLinkingLayerCreator linkerBuilder;
    if ((target.arch == Target::Arch::X86 && target.bits == 32) ||
        (target.arch == Target::Arch::ARM && target.bits == 32)) {
        // Fallback to RTDyld-based linking to workaround errors:
        // i386: "JIT session error: Unsupported i386 relocation:4" (R_386_PLT32)
        // ARM 32bit: Unsupported target machine architecture in ELF object shared runtime-jitted-objectbuffer
        linkerBuilder = [&](llvm::orc::ExecutionSession &session, const llvm::Triple &) {
            return std::make_unique<llvm::orc::RTDyldObjectLinkingLayer>(session, [&]() {
                return std::make_unique<HalideJITMemoryManager>(dependencies);
            });
        };
    } else {
        linkerBuilder = [](llvm::orc::ExecutionSession &session, const llvm::Triple &) {
            return std::make_unique<llvm::orc::ObjectLinkingLayer>(session);
        };
    }

    auto JIT = llvm::cantFail(llvm::orc::LLJITBuilder()
                                  .setDataLayout(target_data_layout)
                                  .setCompileFunctionCreator(compilerBuilder)
                                  .setObjectLinkingLayerCreator(linkerBuilder)
                                  .create());

    auto ctors = llvm::orc::getConstructors(*m);
    llvm::orc::CtorDtorRunner ctorRunner(JIT->getMainJITDylib());
    ctorRunner.add(ctors);

    auto dtors = llvm::orc::getDestructors(*m);
    auto dtorRunner = std::make_unique<llvm::orc::CtorDtorRunner>(JIT->getMainJITDylib());
    dtorRunner->add(dtors);

    // Resolve system symbols (like pthread, dl and others)
    auto gen = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(target_data_layout.getGlobalPrefix());
    internal_assert(gen) << llvm::toString(gen.takeError()) << "\n";
    JIT->getMainJITDylib().addGenerator(std::move(gen.get()));

    llvm::orc::ThreadSafeModule tsm(std::move(m), std::move(jit_module->context));
    auto err = JIT->addIRModule(std::move(tsm));
    internal_assert(!err) << llvm::toString(std::move(err)) << "\n";

    // Resolve symbol dependencies
    llvm::orc::SymbolMap newSymbols;
    auto symbolStringPool = JIT->getExecutionSession().getExecutorProcessControl().getSymbolStringPool();
    for (const auto &module : dependencies) {
        for (auto const &iter : module.exports()) {
            orc::SymbolStringPtr name = symbolStringPool->intern(iter.first);
            orc::SymbolStringPtr _name = symbolStringPool->intern("_" + iter.first);
#if LLVM_VERSION >= 170
            auto symbol = llvm::orc::ExecutorAddr::fromPtr(iter.second.address);
            if (!newSymbols.count(name)) {
                newSymbols.insert({name, {symbol, JITSymbolFlags::Exported}});
            }
            if (!newSymbols.count(_name)) {
                newSymbols.insert({_name, {symbol, JITSymbolFlags::Exported}});
            }
#else
            auto symbol = llvm::JITEvaluatedSymbol::fromPointer(iter.second.address);
            if (!newSymbols.count(name)) {
                newSymbols.insert({name, symbol});
            }
            if (!newSymbols.count(_name)) {
                newSymbols.insert({_name, symbol});
            }
#endif
        }
    }
    err = JIT->getMainJITDylib().define(orc::absoluteSymbols(std::move(newSymbols)));
    internal_assert(!err) << llvm::toString(std::move(err)) << "\n";

    // Retrieve function pointers from the compiled module (which also
    // triggers compilation)
    debug(1) << "JIT compiling " << module_name
             << " for " << target.to_string() << "\n";

    std::map<std::string, Symbol> exports;

    Symbol entrypoint;
    Symbol argv_entrypoint;
    if (!function_name.empty()) {
        entrypoint = compile_and_get_function(*JIT, function_name);
        exports[function_name] = entrypoint;
        argv_entrypoint = compile_and_get_function(*JIT, function_name + "_argv");
        exports[function_name + "_argv"] = argv_entrypoint;
    }

    for (const auto &requested_export : requested_exports) {
        exports[requested_export] = compile_and_get_function(*JIT, requested_export);
    }

    err = ctorRunner.run();
    internal_assert(!err) << llvm::toString(std::move(err)) << "\n";

    // Stash the various objects that need to stay alive behind a reference-counted pointer.
    jit_module->exports = exports;
    jit_module->JIT = std::move(JIT);
    jit_module->dtorRunner = std::move(dtorRunner);
    jit_module->dependencies = dependencies;
    jit_module->entrypoint = entrypoint;
    jit_module->argv_entrypoint = argv_entrypoint;
    jit_module->name = function_name;
}

/*static*/
JITModule JITModule::make_trampolines_module(const Target &target_arg,
                                             const std::map<std::string, JITExtern> &externs,
                                             const std::string &suffix,
                                             const std::vector<JITModule> &deps) {
    Target target = target_arg;
    target.set_feature(Target::JIT);

    JITModule result;
    std::vector<std::pair<std::string, ExternSignature>> extern_signatures;
    std::vector<std::string> requested_exports;
    for (const std::pair<const std::string, JITExtern> &e : externs) {
        const std::string &callee_name = e.first;
        const std::string wrapper_name = callee_name + suffix;
        const ExternCFunction &extern_c = e.second.extern_c_function();
        result.add_extern_for_export(callee_name, extern_c);
        requested_exports.push_back(wrapper_name);
        extern_signatures.emplace_back(callee_name, extern_c.signature());
    }

    std::unique_ptr<llvm::Module> llvm_module = CodeGen_LLVM::compile_trampolines(
        target, *result.jit_module->context, suffix, extern_signatures);

    result.compile_module(std::move(llvm_module), /*function_name*/ "", target, deps, requested_exports);

    return result;
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
        if (s.address) {
            return s;
        }
    }
    return JITModule::Symbol();
}

void *JITModule::main_function() const {
    return jit_module->entrypoint.address;
}

JITModule::Symbol JITModule::entrypoint_symbol() const {
    return jit_module->entrypoint;
}

int (*JITModule::argv_function() const)(const void *const *) {
    return (int (*)(const void *const *))jit_module->argv_entrypoint.address;
}

JITModule::Symbol JITModule::argv_entrypoint_symbol() const {
    return jit_module->argv_entrypoint;
}

namespace {

bool module_already_in_graph(const JITModuleContents *start, const JITModuleContents *target, std::set<const JITModuleContents *> &already_seen) {
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

}  // namespace

void JITModule::add_dependency(JITModule &dep) {
    std::set<const JITModuleContents *> already_seen;
    internal_assert(!module_already_in_graph(dep.jit_module.get(), jit_module.get(), already_seen)) << "JITModule::add_dependency: creating circular dependency graph.\n";
    jit_module->dependencies.push_back(dep);
}

void JITModule::add_symbol_for_export(const std::string &name, const Symbol &extern_symbol) {
    jit_module->exports[name] = extern_symbol;
}

void JITModule::add_extern_for_export(const std::string &name,
                                      const ExternCFunction &extern_c_function) {
    Symbol symbol(extern_c_function.address());
    jit_module->exports[name] = symbol;
}

void JITModule::memoization_cache_set_size(int64_t size) const {
    std::map<std::string, Symbol>::const_iterator f =
        exports().find("halide_memoization_cache_set_size");
    if (f != exports().end()) {
        (reinterpret_bits<void (*)(int64_t)>(f->second.address))(size);
    }
}

void JITModule::memoization_cache_evict(uint64_t eviction_key) const {
    std::map<std::string, Symbol>::const_iterator f =
        exports().find("halide_memoization_cache_evict");
    if (f != exports().end()) {
        (reinterpret_bits<void (*)(void *, uint64_t)>(f->second.address))(nullptr, eviction_key);
    }
}

void JITModule::reuse_device_allocations(bool b) const {
    std::map<std::string, Symbol>::const_iterator f =
        exports().find("halide_reuse_device_allocations");
    if (f != exports().end()) {
        (reinterpret_bits<int (*)(void *, bool)>(f->second.address))(nullptr, b);
    }
}

bool JITModule::compiled() const {
    return jit_module->JIT != nullptr;
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
    if (addins.custom_cuda_acquire_context) {
        base.custom_cuda_acquire_context = addins.custom_cuda_acquire_context;
    }
    if (addins.custom_cuda_release_context) {
        base.custom_cuda_release_context = addins.custom_cuda_release_context;
    }
    if (addins.custom_cuda_get_stream) {
        base.custom_cuda_get_stream = addins.custom_cuda_get_stream;
    }
}

void print_handler(JITUserContext *context, const char *msg) {
    if (context && context->handlers.custom_print) {
        context->handlers.custom_print(context, msg);
    } else {
        return active_handlers.custom_print(context, msg);
    }
}

void *malloc_handler(JITUserContext *context, size_t x) {
    if (context && context->handlers.custom_malloc) {
        return context->handlers.custom_malloc(context, x);
    } else {
        return active_handlers.custom_malloc(context, x);
    }
}

void free_handler(JITUserContext *context, void *ptr) {
    if (context && context->handlers.custom_free) {
        context->handlers.custom_free(context, ptr);
    } else {
        active_handlers.custom_free(context, ptr);
    }
}

int do_task_handler(JITUserContext *context, int (*f)(JITUserContext *, int, uint8_t *), int idx,
                    uint8_t *closure) {
    if (context && context->handlers.custom_do_task) {
        return context->handlers.custom_do_task(context, f, idx, closure);
    } else {
        return active_handlers.custom_do_task(context, f, idx, closure);
    }
}

int do_par_for_handler(JITUserContext *context, int (*f)(JITUserContext *, int, uint8_t *),
                       int min, int size, uint8_t *closure) {
    if (context && context->handlers.custom_do_par_for) {
        return context->handlers.custom_do_par_for(context, f, min, size, closure);
    } else {
        return active_handlers.custom_do_par_for(context, f, min, size, closure);
    }
}

void error_handler_handler(JITUserContext *context, const char *msg) {
    if (context && context->handlers.custom_error) {
        context->handlers.custom_error(context, msg);
    } else {
        active_handlers.custom_error(context, msg);
    }
}

int32_t trace_handler(JITUserContext *context, const halide_trace_event_t *e) {
    if (context && context->handlers.custom_trace) {
        return context->handlers.custom_trace(context, e);
    } else {
        return active_handlers.custom_trace(context, e);
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

int cuda_acquire_context_handler(JITUserContext *context, void **cuda_context_ptr, bool create) {
    if (context && context->handlers.custom_cuda_acquire_context) {
        return context->handlers.custom_cuda_acquire_context(context, cuda_context_ptr, create);
    } else {
        return active_handlers.custom_cuda_acquire_context(context, cuda_context_ptr, create);
    }
}

int cuda_release_context_handler(JITUserContext *context) {
    if (context && context->handlers.custom_cuda_release_context) {
        return context->handlers.custom_cuda_release_context(context);
    } else {
        return active_handlers.custom_cuda_release_context(context);
    }
}

int cuda_get_stream_handler(JITUserContext *context, void *cuda_context, void **cuda_stream_ptr) {
    if (context && context->handlers.custom_cuda_get_stream) {
        return context->handlers.custom_cuda_get_stream(context, cuda_context, cuda_stream_ptr);
    } else {
        return active_handlers.custom_cuda_get_stream(context, cuda_context, cuda_stream_ptr);
    }
}

template<typename function_t>
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
    Hexagon,
    D3D12Compute,
    Vulkan,
    WebGPU,
    OpenCLDebug,
    MetalDebug,
    CUDADebug,
    HexagonDebug,
    D3D12ComputeDebug,
    VulkanDebug,
    WebGPUDebug,
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
        // msan doesn't work for jit modules
        target.set_feature(Target::MSAN, false);

        Target one_gpu(target);
        one_gpu.set_feature(Target::Debug, false);
        one_gpu.set_feature(Target::OpenCL, false);
        one_gpu.set_feature(Target::Metal, false);
        one_gpu.set_feature(Target::CUDA, false);
        one_gpu.set_feature(Target::HVX, false);
        one_gpu.set_feature(Target::D3D12Compute, false);
        one_gpu.set_feature(Target::Vulkan, false);
        one_gpu.set_feature(Target::WebGPU, false);
        string module_name;
        switch (runtime_kind) {
        case OpenCLDebug:
            one_gpu.set_feature(Target::Debug);
            one_gpu.set_feature(Target::OpenCL);
            module_name = "debug_opencl";
            break;
        case OpenCL:
            one_gpu.set_feature(Target::OpenCL);
            module_name += "opencl";
            break;
        case MetalDebug:
            one_gpu.set_feature(Target::Debug);
            one_gpu.set_feature(Target::Metal);
            load_metal();
            module_name = "debug_metal";
            break;
        case Metal:
            one_gpu.set_feature(Target::Metal);
            module_name += "metal";
            load_metal();
            break;
        case CUDADebug:
            one_gpu.set_feature(Target::Debug);
            one_gpu.set_feature(Target::CUDA);
            module_name = "debug_cuda";
            break;
        case CUDA:
            one_gpu.set_feature(Target::CUDA);
            module_name += "cuda";
            break;
        case HexagonDebug:
            one_gpu.set_feature(Target::Debug);
            one_gpu.set_feature(Target::HVX);
            module_name = "debug_hexagon";
            break;
        case Hexagon:
            one_gpu.set_feature(Target::HVX);
            module_name += "hexagon";
            break;
        case D3D12ComputeDebug:
            one_gpu.set_feature(Target::Debug);
            one_gpu.set_feature(Target::D3D12Compute);
            module_name = "debug_d3d12compute";
            break;
        case D3D12Compute:
            one_gpu.set_feature(Target::D3D12Compute);
            module_name += "d3d12compute";
#if !defined(_WIN32)
            internal_error << "JIT support for Direct3D 12 is only implemented on Windows 10 and above.\n";
#endif
            break;
        case VulkanDebug:
            one_gpu.set_feature(Target::Debug);
            one_gpu.set_feature(Target::Vulkan);
            load_vulkan();
            module_name = "debug_vulkan";
            break;
        case Vulkan:
            one_gpu.set_feature(Target::Vulkan);
            load_vulkan();
            module_name += "vulkan";
            break;
        case WebGPUDebug:
            one_gpu.set_feature(Target::Debug);
            one_gpu.set_feature(Target::WebGPU);
            module_name = "debug_webgpu";
            load_webgpu();
            break;
        case WebGPU:
            one_gpu.set_feature(Target::WebGPU);
            module_name += "webgpu";
            load_webgpu();
            break;
        default:
            module_name = "shared runtime";
            break;
        }

        // This function is protected by a mutex so this is thread safe.
        auto module =
            get_initial_module_for_target(one_gpu,
                                          runtime.jit_module->context.get(),
                                          true,
                                          runtime_kind != MainShared);
        if (for_module) {
            clone_target_options(*for_module, *module);
        }
        module->setModuleIdentifier(module_name);

        std::set<std::string> halide_exports_unique;

        // Enumerate the functions.
        for (auto &f : *module) {
            // LLVM_Runtime_Linker has marked everything that should be exported as weak
            if (f.hasWeakLinkage()) {
                halide_exports_unique.insert(get_llvm_function_name(f));
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
                hook_function(runtime.exports(), "halide_set_custom_get_symbol", get_symbol_handler);

            runtime_internal_handlers.custom_load_library =
                hook_function(runtime.exports(), "halide_set_custom_load_library", load_library_handler);

            runtime_internal_handlers.custom_get_library_symbol =
                hook_function(runtime.exports(), "halide_set_custom_get_library_symbol", get_library_symbol_handler);

            active_handlers = runtime_internal_handlers;
            merge_handlers(active_handlers, default_handlers);

            if (default_cache_size != 0) {
                runtime.memoization_cache_set_size(default_cache_size);
            }

            runtime.jit_module->name = "MainShared";
        } else {
            runtime.jit_module->name = "GPU";

            // There are two versions of these cuda context
            // management handlers we could use - one in the cuda
            // module, and one in the cuda-debug module. If both
            // modules are in use, we'll just want to use one of
            // them, so that we don't needlessly create two cuda
            // contexts. We'll use whichever was first
            // created. The second one will then declare a
            // dependency on the first one, to make sure things
            // are destroyed in the correct order.

            if (runtime_kind == CUDA || runtime_kind == CUDADebug) {
                if (!runtime_internal_handlers.custom_cuda_acquire_context) {
                    // Neither module has been created.
                    runtime_internal_handlers.custom_cuda_acquire_context =
                        hook_function(runtime.exports(), "halide_set_cuda_acquire_context", cuda_acquire_context_handler);

                    runtime_internal_handlers.custom_cuda_release_context =
                        hook_function(runtime.exports(), "halide_set_cuda_release_context", cuda_release_context_handler);

                    runtime_internal_handlers.custom_cuda_get_stream =
                        hook_function(runtime.exports(), "halide_set_cuda_get_stream", cuda_get_stream_handler);

                    active_handlers = runtime_internal_handlers;
                    merge_handlers(active_handlers, default_handlers);
                } else if (runtime_kind == CUDA) {
                    // The CUDADebug module has already been created.
                    // Use the context in the CUDADebug module and add
                    // a dependence edge from the CUDA module to it.
                    runtime.add_dependency(shared_runtimes(CUDADebug));
                } else {
                    // The CUDA module has already been created.
                    runtime.add_dependency(shared_runtimes(CUDA));
                }
            }
        }

        uint64_t arg_addr = llvm::cantFail(runtime.jit_module->JIT->lookup("halide_jit_module_argument"))
                                .getValue();
        internal_assert(arg_addr != 0);
        *((void **)arg_addr) = runtime.jit_module.get();

        uint64_t fun_addr = llvm::cantFail(runtime.jit_module->JIT->lookup("halide_jit_module_adjust_ref_count"))
                                .getValue();
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
        auto kind = target.has_feature(Target::Debug) ? OpenCLDebug : OpenCL;
        JITModule m = make_module(for_module, target, kind, result, create);
        if (m.compiled()) {
            result.push_back(m);
        }
    }
    if (target.has_feature(Target::Metal)) {
        auto kind = target.has_feature(Target::Debug) ? MetalDebug : Metal;
        JITModule m = make_module(for_module, target, kind, result, create);
        if (m.compiled()) {
            result.push_back(m);
        }
    }
    if (target.has_feature(Target::CUDA)) {
        auto kind = target.has_feature(Target::Debug) ? CUDADebug : CUDA;
        JITModule m = make_module(for_module, target, kind, result, create);
        if (m.compiled()) {
            result.push_back(m);
        }
    }
    if (target.has_feature(Target::HVX)) {
        auto kind = target.has_feature(Target::Debug) ? HexagonDebug : Hexagon;
        JITModule m = make_module(for_module, target, kind, result, create);
        if (m.compiled()) {
            result.push_back(m);
        }
    }
    if (target.has_feature(Target::D3D12Compute)) {
        auto kind = target.has_feature(Target::Debug) ? D3D12ComputeDebug : D3D12Compute;
        JITModule m = make_module(for_module, target, kind, result, create);
        if (m.compiled()) {
            result.push_back(m);
        }
    }
    if (target.has_feature(Target::Vulkan)) {
        auto kind = target.has_feature(Target::Debug) ? VulkanDebug : Vulkan;
        JITModule m = make_module(for_module, target, kind, result, create);
        if (m.compiled()) {
            result.push_back(m);
        }
    }
    if (target.has_feature(Target::WebGPU)) {
        auto kind = target.has_feature(Target::Debug) ? WebGPUDebug : WebGPU;
        JITModule m = make_module(for_module, target, kind, result, create);
        if (m.compiled()) {
            result.push_back(m);
        }
    }
    return result;
}

void JITSharedRuntime::populate_jit_handlers(JITUserContext *jit_user_context, const JITHandlers &handlers) {
    // Take the active global handlers
    JITHandlers merged = active_handlers;
    // Clobber with any custom handlers set on the pipeline
    merge_handlers(merged, handlers);
    // Clobber with any custom handlers set on the call
    merge_handlers(merged, jit_user_context->handlers);
    jit_user_context->handlers = merged;
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

void JITSharedRuntime::memoization_cache_evict(uint64_t eviction_key) {
    std::lock_guard<std::mutex> lock(shared_runtimes_mutex);
    shared_runtimes(MainShared).memoization_cache_evict(eviction_key);
}

void JITSharedRuntime::reuse_device_allocations(bool b) {
    std::lock_guard<std::mutex> lock(shared_runtimes_mutex);
    shared_runtimes(MainShared).reuse_device_allocations(b);
}

JITCache::JITCache(Target jit_target,
                   std::vector<Argument> arguments,
                   std::map<std::string, JITExtern> jit_externs,
                   JITModule jit_module,
                   WasmModule wasm_module)
    : jit_target(jit_target),  // clang-tidy complains that this is "trivially copyable" and std::move shouldn't be here, grr
      arguments(std::move(arguments)),
      jit_externs(std::move(jit_externs)),
      jit_module(std::move(jit_module)),
      wasm_module(std::move(wasm_module)) {
}

Target JITCache::get_compiled_jit_target() const {
    // This essentially is just a getter for contents->jit_target,
    // but also reality-checks that the status of the jit_module and/or wasm_module
    // match what we expect.
    const bool has_wasm = wasm_module.contents.defined();
    const bool has_native = jit_module.compiled();
    if (jit_target.arch == Target::WebAssembly) {
        internal_assert(has_wasm && !has_native);
    } else if (!jit_target.has_unknowns()) {
        internal_assert(!has_wasm && has_native);
    } else {
        internal_assert(!has_wasm && !has_native);
    }
    return jit_target;
}

int JITCache::call_jit_code(const void *const *args) {
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
    user_warning << "MSAN does not support JIT compilers of any sort, and will report "
                    "false positives when used in conjunction with the Halide JIT. "
                    "If you need to test with MSAN enabled, you must use ahead-of-time "
                    "compilation for Halide code.";
#endif
#endif
    if (get_compiled_jit_target().arch == Target::WebAssembly) {
        internal_assert(wasm_module.contents.defined());
        return wasm_module.run(args);
    } else {
        auto argv_wrapper = jit_module.argv_function();
        internal_assert(argv_wrapper != nullptr);
        return argv_wrapper(args);
    }
}

void JITCache::finish_profiling(JITUserContext *context) {
    // If we're profiling, report runtimes and reset profiler stats.
    if (jit_target.has_feature(Target::Profile) || jit_target.has_feature(Target::ProfileByTimer)) {
        JITModule::Symbol report_sym = jit_module.find_symbol_by_name("halide_profiler_report");
        JITModule::Symbol reset_sym = jit_module.find_symbol_by_name("halide_profiler_reset");
        if (report_sym.address && reset_sym.address) {
            void (*report_fn_ptr)(JITUserContext *) = (void (*)(JITUserContext *))(report_sym.address);
            report_fn_ptr(context);

            void (*reset_fn_ptr)() = (void (*)())(reset_sym.address);
            reset_fn_ptr();
        }
    }
}

void JITErrorBuffer::concat(const char *message) {
    size_t len = strlen(message);

    if (len && message[len - 1] != '\n') {
        // Claim some extra space for a newline.
        len++;
    }

    // Atomically claim some space in the buffer
    size_t old_end = end.fetch_add(len);

    if (old_end + len >= MaxBufSize - 1) {
        // Out of space
        return;
    }

    for (size_t i = 0; i < len - 1; i++) {
        buf[old_end + i] = message[i];
    }
    if (buf[old_end + len - 2] != '\n') {
        buf[old_end + len - 1] = '\n';
    }
}

std::string JITErrorBuffer::str() const {
    return std::string(buf, end);
}

/*static*/ void JITErrorBuffer::handler(JITUserContext *ctx, const char *message) {
    if (ctx && ctx->error_buffer) {
        ctx->error_buffer->concat(message);
    }
}

JITFuncCallContext::JITFuncCallContext(JITUserContext *context, const JITHandlers &pipeline_handlers)
    : context(context) {
    custom_error_handler = ((context->handlers.custom_error != nullptr &&
                             context->handlers.custom_error != JITErrorBuffer::handler) ||
                            pipeline_handlers.custom_error != nullptr);
    // Hook the error handler if not set
    if (!custom_error_handler) {
        context->handlers.custom_error = JITErrorBuffer::handler;
    }

    // Add the handlers stored in the pipeline for anything else
    // not set, then for anything still not set, use the global
    // active handlers.
    JITSharedRuntime::populate_jit_handlers(context, pipeline_handlers);
    context->error_buffer = &error_buffer;

    debug(2) << "custom_print: " << (void *)context->handlers.custom_print << "\n"
             << "custom_malloc: " << (void *)context->handlers.custom_malloc << "\n"
             << "custom_free: " << (void *)context->handlers.custom_free << "\n"
             << "custom_do_task: " << (void *)context->handlers.custom_do_task << "\n"
             << "custom_do_par_for: " << (void *)context->handlers.custom_do_par_for << "\n"
             << "custom_error: " << (void *)context->handlers.custom_error << "\n"
             << "custom_trace: " << (void *)context->handlers.custom_trace << "\n";
}

void JITFuncCallContext::finalize(int exit_status) {
    // Only report the errors if no custom error handler was installed
    if (exit_status && !custom_error_handler) {
        std::string output = error_buffer.str();
        if (output.empty()) {
            output = ("The pipeline returned exit status " +
                      std::to_string(exit_status) +
                      " but halide_error was never called.\n");
        }
        halide_runtime_error << output;
        error_buffer.end = 0;
    }
}

}  // namespace Internal
}  // namespace Halide
