#include <string>
#include <stdint.h>

#if __cplusplus > 199711L || _MSC_VER >= 1800
#include <mutex>
#endif

#include "buffer_t.h"
#include "JITModule.h"
#include "CodeGen.h"
#include "LLVM_Headers.h"
#include "Debug.h"

namespace Halide {
namespace Internal {

using std::string;

using namespace llvm;

llvm::Type *copy_llvm_type_to_module(Module *to_module, llvm::Type *from_type) {
    LLVMContext &context(to_module->getContext());
    if (&from_type->getContext() == &context) {
        return from_type;
    }

    switch (from_type->getTypeID()) {
    case llvm::Type::VoidTyID:
        return llvm::Type::getVoidTy(context);
        break;
    case llvm::Type::HalfTyID:
        return llvm::Type::getHalfTy(context);
        break;
    case llvm::Type::FloatTyID:
        return llvm::Type::getFloatTy(context);
        break;
    case llvm::Type::DoubleTyID:
        return llvm::Type::getDoubleTy(context);
        break;
    case llvm::Type::X86_FP80TyID:
        return llvm::Type::getX86_FP80Ty(context);
        break;
    case llvm::Type::FP128TyID:
        return llvm::Type::getFP128Ty(context);
        break;
    case llvm::Type::PPC_FP128TyID:
        return llvm::Type::getPPC_FP128Ty(context);
        break;
    case llvm::Type::LabelTyID:
        return llvm::Type::getLabelTy(context);
        break;
    case llvm::Type::MetadataTyID:
        return llvm::Type::getMetadataTy(context);
        break;
    case llvm::Type::X86_MMXTyID:
        return llvm::Type::getX86_MMXTy(context);
        break;
    case llvm::Type::IntegerTyID:
        return llvm::Type::getIntNTy(context, from_type->getIntegerBitWidth());
        break;
    case llvm::Type::FunctionTyID: {
        FunctionType *f = cast<FunctionType>(from_type);
        llvm::Type *return_type = copy_llvm_type_to_module(to_module, f->getReturnType());
        std::vector<llvm::Type *> arg_types;
        for (size_t i = 0; i < f->getNumParams(); i++) {
            arg_types.push_back(copy_llvm_type_to_module(to_module, f->getParamType(i)));
        }
        return FunctionType::get(return_type, arg_types, f->isVarArg());
    } break;
    case llvm::Type::StructTyID: {
        StructType *result;
        StructType *s = cast<StructType>(from_type);
        std::vector<llvm::Type *> element_types;
        for (size_t i = 0; i < s->getNumElements(); i++) {
            element_types.push_back(copy_llvm_type_to_module(to_module, s->getElementType(i)));
        }
        if (s->isLiteral()) {
            result = StructType::get(context, element_types, s->isPacked());
        } else {
            result = to_module->getTypeByName(s->getName());
            if (result == NULL) {
                result = StructType::create(context, s->getName());
                if (!element_types.empty()) {
                    result->setBody(element_types, s->isPacked());
                }
            } else {
                if (result->isOpaque() &&
                    !element_types.empty()) {
                    result->setBody(element_types, s->isPacked());
                }
            }
        }
        return result;
    } break;
    case llvm::Type::ArrayTyID: {
        ArrayType *a = cast<ArrayType>(from_type);
        return ArrayType::get(copy_llvm_type_to_module(to_module, a->getElementType()), a->getNumElements());
    } break;
    case llvm::Type::PointerTyID: {
        PointerType *p = cast<PointerType>(from_type);
        return PointerType::get(copy_llvm_type_to_module(to_module, p->getElementType()), p->getAddressSpace());
    } break;
    case llvm::Type::VectorTyID: {
        VectorType *v = cast<VectorType>(from_type);
        return VectorType::get(copy_llvm_type_to_module(to_module, v->getElementType()), v->getNumElements());
    } break;
    default: {
        internal_error << "Unhandled LLVM type\n";
        return NULL;
    }
    }

}

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
                      const std::map<std::string, void *> &runtime_internal_exports,
                      llvm::ExecutionEngine *ee, llvm::Module *m, const std::vector<JITModule> &dependencies,
                      void *main_function = NULL, int (*jit_wrapper_function)(const void **) = NULL) : exports(exports),
                                                                                                       runtime_internal_exports(runtime_internal_exports),
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
            delete context;
            // No need to delete the module - deleting the execution engine should take care of that.
        }
    }

    std::map<std::string, JITModule::Symbol> exports;
    std::map<std::string, void *> runtime_internal_exports;

    ExecutionEngine *execution_engine;
    Module *module;
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

#ifdef __arm__
// On ARM we need to track the addresses of all the functions we
// retrieve so that we can flush the icache.
char *start, *end;
#endif

// Retrieve a function pointer from an llvm module, possibly by
// compiling it. Returns it by assigning to the last argument.

JITModule::Symbol compile_and_get_function(ExecutionEngine *ee, Module *mod, const string &name, bool optional = false) {
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

// TODO: Does this need to be conditionalized to llvm 3.6?
class HalideJITMemoryManager : public SectionMemoryManager {
    std::vector<JITModule> dependencies;

public:
    HalideJITMemoryManager(const std::vector<JITModule> &dependencies) : dependencies(dependencies) {}

    virtual uint64_t getSymbolAddress(const std::string &name) {
        for (size_t i = 0; i < dependencies.size(); i++) {
            std::map<std::string, JITModule::Symbol>::const_iterator iter = dependencies[i].exports().find(name);
            if (iter == dependencies[i].exports().end() && starts_with(name, "_")) {
                iter = dependencies[i].exports().find(name.substr(1));
            }
            if (iter != dependencies[i].exports().end()) {
                return (uint64_t)iter->second.address;
            }
        }
        return SectionMemoryManager::getSymbolAddress(name);
    }
};

}

void JITModule::compile_module(CodeGen *cg, llvm::Module *m, const string &function_name,
                               const std::vector<JITModule> &dependencies,
                               const std::vector<std::string> &requested_exports) {

    // Set the target triple on the module.
    m->setTargetTriple(cg->get_target_triple().str());

    // Make the execution engine
    debug(2) << "Creating new execution engine\n";
    debug(2) << "Target triple: " << m->getTargetTriple() << "\n";
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
                    llvm::Type *copied_type = copy_llvm_type_to_module(m, s.llvm_type);
                    m->getOrInsertFunction(name, cast<FunctionType>(copied_type));
                } else {
                    m->getOrInsertGlobal(name, copy_llvm_type_to_module(m, s.llvm_type));
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

    void *main_fn;
    int (*wrapper_fn)(const void **);
    if (!function_name.empty()) {
        Symbol temp;
        exports[function_name] = temp = compile_and_get_function(ee, m, function_name);
        main_fn = temp.address;
        exports[function_name + "_jit_wrapper"] = temp = compile_and_get_function(ee, m, function_name + "_jit_wrapper");
        wrapper_fn = reinterpret_bits<int (*)(const void **)>(temp.address);
    }

    for (size_t i = 0; i < requested_exports.size(); i++) {
        exports[requested_exports[i]] = compile_and_get_function(ee, m, requested_exports[i]);
    }

    const char *runtime_internal_names[] = {
        "halide_dev_malloc",
        "halide_dev_free",
        "halide_copy_to_dev",
        "halide_copy_to_host",
        "halide_set_custom_print",
        "halide_set_error_handler",
        "halide_set_custom_malloc",
        "halide_set_custom_free",
        "halide_set_custom_trace",
        "halide_set_custom_do_par_for",
        "halide_set_custom_do_task",
        "halide_memoization_cache_set_size",
        NULL
    };

    std::map<std::string, void *> runtime_internal_exports;
    for (size_t i = 0; runtime_internal_names[i]; i++) {
        void *address = compile_and_get_function(ee, m, runtime_internal_names[i], true).address;
        if (address) {
            runtime_internal_exports[runtime_internal_names[i]] = address;
        }
    }

    debug(2) << "Finalizing object\n";
    ee->finalizeObject();

    // Stash the various objects that need to stay alive behind a reference-counted pointer.
    jit_module = new JITModuleContents(exports, runtime_internal_exports, ee, m, dependencies, main_fn, wrapper_fn);
    jit_module.ptr->name = function_name;

    // Do any target-specific post-compilation module meddling
    cg->jit_finalize(ee, m);

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
                gv = (llvm::Function *)module->getOrInsertFunction(name, (FunctionType *)copy_llvm_type_to_module(module, s.llvm_type));
            } else {
                gv = (GlobalValue *)module->getOrInsertGlobal(name, copy_llvm_type_to_module(module, s.llvm_type));
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

int JITModule::copy_to_dev(struct buffer_t *buf) const {
    if (jit_module.defined()) {
        std::map<std::string, void *>::const_iterator f =
            jit_module.ptr->runtime_internal_exports.find("halide_copy_to_dev");
        if (f != jit_module.ptr->runtime_internal_exports.end()) {
            return (reinterpret_bits<int (*)(void *, struct buffer_t *)>(f->second))(NULL, buf);
        }
    }
    return 0;
}

int JITModule::copy_to_host(struct buffer_t *buf) const {
    if (jit_module.defined()) {
        std::map<std::string, void *>::const_iterator f =
            jit_module.ptr->runtime_internal_exports.find("halide_copy_to_host");
        if (f != jit_module.ptr->runtime_internal_exports.end()) {
            return (reinterpret_bits<int (*)(void *, struct buffer_t *)>(f->second))(NULL, buf);
        }
    }
    return 0;
}

int JITModule::dev_free(struct buffer_t *buf) const {
    if (jit_module.defined()) {
        std::map<std::string, void *>::const_iterator f =
            jit_module.ptr->runtime_internal_exports.find("halide_dev_free");
        if (f != jit_module.ptr->runtime_internal_exports.end()) {
            return (reinterpret_bits<int (*)(void *, struct buffer_t *)>(f->second))(NULL, buf);
        }
    }
    return 0;
}

void JITModule::memoization_cache_set_size(int64_t size) const {
    if (jit_module.defined()) {
        std::map<std::string, void *>::const_iterator f =
            jit_module.ptr->runtime_internal_exports.find("halide_memoization_cache_set_size");
        if (f != jit_module.ptr->runtime_internal_exports.end()) {
            return (reinterpret_bits<void (*)(int64_t)>(f->second))(size);
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
    CUDA,
    OpenCL,
    OpenGL,
    NoGPU,
    MaxRuntimeKind
};

JITModule shared_runtimes[MaxRuntimeKind];

JITModule &make_module(CodeGen *cg, const Target &target, RuntimeKind runtime_kind, const std::vector<JITModule> &deps) {
    if (!shared_runtimes[runtime_kind].jit_module.defined()) {
        LLVMContext *llvm_context = new LLVMContext();

        llvm::Module *shared_runtime = 
            get_initial_module_for_target(target, llvm_context, true, runtime_kind != MainShared);

        std::set<std::string> halide_exports_unique;

        // Enumerate the functions.
        for (Module::const_iterator iter = shared_runtime->begin(); iter != shared_runtime->end(); iter++) {
            const llvm::Function *gv = cast<llvm::Function>(iter);
            if (gv->hasWeakLinkage() && starts_with(gv->getName(), "halide_")) {
                halide_exports_unique.insert(gv->getName());
            }
        }

        std::vector<std::string> halide_exports(halide_exports_unique.begin(), halide_exports_unique.end());

        shared_runtimes[runtime_kind].compile_module(cg, shared_runtime, "", deps, halide_exports);

        if (runtime_kind == MainShared) {
            runtime_internal_handlers.custom_print = 
                hook_function(shared_runtimes[MainShared].exports(), "halide_set_custom_print", print_handler);

            runtime_internal_handlers.custom_malloc =
                hook_function(shared_runtimes[MainShared].exports(), "halide_set_custom_malloc", malloc_handler);

            runtime_internal_handlers.custom_free =
                hook_function(shared_runtimes[MainShared].exports(), "halide_set_custom_free", free_handler);

            runtime_internal_handlers.custom_do_task = 
                hook_function(shared_runtimes[MainShared].exports(), "halide_set_custom_do_task", do_task_handler);

            runtime_internal_handlers.custom_do_par_for = 
                hook_function(shared_runtimes[MainShared].exports(), "halide_set_custom_do_par_for", do_par_for_handler);

            runtime_internal_handlers.custom_error = 
                hook_function(shared_runtimes[MainShared].exports(), "halide_set_error_handler", error_handler_handler);

            runtime_internal_handlers.custom_trace = 
                hook_function(shared_runtimes[MainShared].exports(), "halide_set_custom_trace", trace_handler);

            active_handlers = runtime_internal_handlers;
            merge_handlers(active_handlers, default_handlers);

            if (default_cache_size != 0) {
                shared_runtimes[MainShared].memoization_cache_set_size(default_cache_size);
            }

            shared_runtimes[runtime_kind].jit_module.ptr->name = "MainShared";
        } else {
            shared_runtimes[runtime_kind].jit_module.ptr->name = "GPU";
        }

        uint64_t arg_addr = 
            shared_runtimes[runtime_kind].jit_module.ptr->execution_engine->getGlobalValueAddress("halide_jit_module_argument");
        internal_assert(arg_addr != 0);
        *((void **)arg_addr) = shared_runtimes[runtime_kind].jit_module.ptr;

        uint64_t fun_addr = shared_runtimes[runtime_kind].jit_module.ptr->execution_engine->getGlobalValueAddress("halide_jit_module_adjust_ref_count");
        internal_assert(fun_addr != 0);
        *(void (**)(void *arg, int32_t count))fun_addr = &adjust_module_ref_count;
    }
    return shared_runtimes[runtime_kind];
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
std::vector<JITModule> JITSharedRuntime::get(CodeGen *cg, const Target &target) {
    #if __cplusplus > 199711L || _MSC_VER >= 1800
    std::lock_guard<std::mutex> lock(shared_runtimes_mutex);
    #endif

    // TODO: Thread safety
    std::vector<JITModule> result;

    result.push_back(make_module(cg, target, MainShared, result));

    JITModule gpu_runtime;
    if (target.has_feature(Target::OpenCL)) {
        gpu_runtime = make_module(cg, target, OpenCL, result);
    } else if (target.has_feature(Target::CUDA)) {
        gpu_runtime = make_module(cg, target, CUDA, result);
    } else if (target.has_feature(Target::OpenGL)) {
        gpu_runtime = make_module(cg, target, OpenGL, result);
    } else {
        gpu_runtime = make_module(cg, target, NoGPU, result);
    }
    result.push_back(gpu_runtime);
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
        shared_runtimes[(RuntimeKind)(i - 1)].jit_module = NULL;
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
        shared_runtimes[MainShared].jit_module.defined()) {
        default_cache_size = size;
        shared_runtimes[MainShared].memoization_cache_set_size(size);
    }
}


}
}
