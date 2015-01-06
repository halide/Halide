#include <string>
#include <stdint.h>

#include "buffer_t.h"
#include "JITModule.h"
#include "CodeGen.h"
#include "LLVM_Headers.h"
#include "Debug.h"

namespace Halide {
namespace Internal {

using std::string;

using namespace llvm;

llvm::Type *copy_llvm_type_to_context(LLVMContext &context, llvm::Type *from_type) {
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
        case llvm::Type::FunctionTyID:
            {
                FunctionType *f = cast<FunctionType>(from_type);
                llvm::Type *return_type = copy_llvm_type_to_context(context, f->getReturnType());
                std::vector<llvm::Type *> arg_types;
                for (size_t i = 0; i < f->getNumParams(); i++) {
                    arg_types.push_back(copy_llvm_type_to_context(context, f->getParamType(i)));
                }
                return FunctionType::get(return_type, arg_types, f->isVarArg());
           }
           break;
        case llvm::Type::StructTyID:
            {
                StructType *s = cast<StructType>(from_type);
                std::vector<llvm::Type *> element_types;
                for (size_t i = 0; i < s->getNumElements(); i++) {
                    element_types.push_back(copy_llvm_type_to_context(context, s->getElementType(i)));
                }
                if (s->isLiteral()) {
                    return StructType::get(context, element_types, s->isPacked());
                } else if (element_types.empty()) {
                    return StructType::create(context, s->getName());
                } else {
                    return StructType::create(context, element_types, s->getName(), s->isPacked());
                }
            }
            break;
        case llvm::Type::ArrayTyID:
            {
                ArrayType *a = cast<ArrayType>(from_type);
                return ArrayType::get(copy_llvm_type_to_context(context, a->getElementType()), a->getNumElements());
            }
            break;
        case llvm::Type::PointerTyID:
            {
                PointerType *p = cast<PointerType>(from_type);
                return PointerType::get(copy_llvm_type_to_context(context, p->getElementType()), p->getAddressSpace());
            }
            break;
        case llvm::Type::VectorTyID:
            {
                VectorType *v = cast<VectorType>(from_type);
                return VectorType::get(copy_llvm_type_to_context(context, v->getElementType()), v->getNumElements());
            }
            break;
    }
    internal_error << "Unhandled LLVM type\n";
    return NULL;
}

// TODO: Figure out if shutdown_thread_pool was necessary and whether it can be a destructor or a CleanupRoutine?
// TODO: Interface for cleanup routines?
// TODO: CleanupRoutines interface with CodeGen
class JITModuleContents {
public:
    mutable RefCount ref_count;

    std::map<std::string, JITModule::Symbol> exports;

  // Just construct a module with symbols to import into other modules.
  JITModuleContents(const std::map<std::string, JITModule::Symbol> &exports) :
        exports(exports),
        execution_engine(NULL),
        module(NULL),
        context(NULL),
        main_function(NULL),
        jit_wrapper_function(NULL) {
    }

  JITModuleContents(const std::map<std::string, JITModule::Symbol> &exports,
                    llvm::ExecutionEngine *ee, llvm::Module *m,
                    void *main_function = NULL, int (*jit_wrapper_function)(const void **) = NULL) :
        exports(exports),
        execution_engine(ee),
        module(m),
        context(&m->getContext()),
        main_function(main_function),
        jit_wrapper_function(jit_wrapper_function) {
    }

    ~JITModuleContents() {
        debug(2) << "Destroying JIT compiled module at " << this << "\n";
#if 0
        for (size_t i = 0; i < cleanup_routines.size(); i++) {
            void *ptr = reinterpret_bits<void *>(cleanup_routines[i].fn);
            debug(2) << "  Calling cleanup routine [" << ptr << "]("
                     << cleanup_routines[i].context << ")\n";
            cleanup_routines[i].fn(cleanup_routines[i].context);
        }
#endif
        if (execution_engine != NULL) {
            execution_engine->runStaticConstructorsDestructors(true);
            delete execution_engine;
            delete context;
            // No need to delete the module - deleting the execution engine should take care of that.
        }
    }

    ExecutionEngine *execution_engine;
    Module *module;
    LLVMContext *context;
    void *main_function;
    int (*jit_wrapper_function)(const void **);

#if 0
    /** Do any target-specific module cleanup. */
    std::vector<JITModule::CleanupRoutine> cleanup_routines;
#endif
};

template<>
EXPORT RefCount &ref_count<JITModuleContents>(const JITModuleContents *f) {return f->ref_count;}

template<>
EXPORT void destroy<JITModuleContents>(const JITModuleContents *f) {delete f;}

namespace {

#ifdef __arm__
// On ARM we need to track the addresses of all the functions we
// retrieve so that we can flush the icache.
char *start, *end;
#endif

// Retrieve a function pointer from an llvm module, possibly by
// compiling it. Returns it by assigning to the last argument.
JITModule::Symbol compile_and_get_function(ExecutionEngine *ee, Module *mod, const string &name) {
    internal_assert(mod && ee);

    debug(2) << "Retrieving " << name << " from module\n";
    llvm::Function *fn = mod->getFunction(name);
    if (!fn) {
        internal_error << "Could not find function " << name << " in module\n";
    }

    debug(2) << "JIT Compiling " << name << "\n";
    void *f = ee->getPointerToFunction(fn);
    if (!f) {
        internal_error << "Compiling " << name << " returned NULL\n";
    }

    JITModule::Symbol symbol;
    symbol.address = f;
    symbol.llvm_type = fn->getFunctionType();

    debug(2) << "Function " << name << " is at " << f << "\n";

    #ifdef __arm__
    if (start == NULL) {
        start = (char *)f;
        end = (char *)f;
    } else {
        start = std::min(start, (char *)f);
        end = std::max(end, (char *)f+32);
    }
    #endif

    return symbol;
}

// TODO: Does this need to be conditionalized to llvm 3.6?
class HalideJITMemoryManager : public SectionMemoryManager {
    std::vector<JITModule> dependencies;
public:
    HalideJITMemoryManager(const std::vector<JITModule> &dependencies) : dependencies(dependencies) { }

    virtual uint64_t getSymbolAddress(const std::string &Name) {
        for (size_t i = 0; i < dependencies.size(); i++) {
            std::map<std::string, JITModule::Symbol>::const_iterator iter = dependencies[i].Exports().find(Name);
            if (iter == dependencies[i].Exports().end() && starts_with(Name, "_")) {
                iter =  dependencies[i].Exports().find(Name.substr(1));
            }
            if (iter != dependencies[i].Exports().end()) {
                debug(0) << "getSymbolAddress found symbol " << Name << "in dependencies as " << iter->first << "\n";
                return (uint64_t)iter->second.address;
            }
        }
        return SectionMemoryManager::getSymbolAddress(Name);
    }
};

}

void JITModule::compile_module(CodeGen *cg, llvm::Module *m, const string &function_name,
                               const std::vector<JITModule> &dependencies,
                               const std::vector<std::string> &requested_exports) {

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
    #else
    engine_builder.setMCJITMemoryManager(std::unique_ptr<RTDyldMemoryManager>(new HalideJITMemoryManager(dependencies)));
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

    // Add exported symbols for all dependencies.
    std::set<std::string> provided_symbols;
    for (size_t i = 0; i < dependencies.size(); i++) {
        const std::map<std::string, Symbol> &dep_exports(dependencies[i].Exports());
        std::map<std::string, Symbol>::const_iterator iter;
        for (iter = dep_exports.begin(); iter != dep_exports.end(); iter++) {
            const std::string &name(iter->first);
            const Symbol &s(iter->second);
            if (provided_symbols.find(iter->first) == provided_symbols.end()) {
              debug(0) << "Adding mapping for " << name << "\n";
                GlobalValue *gv;
                if (s.llvm_type->isFunctionTy()) {
                    if (!starts_with(name, "_")) {
                        gv = (llvm::Function *)m->getOrInsertFunction("_" + name, (FunctionType *)copy_llvm_type_to_context(m->getContext(), s.llvm_type));
                        ee->addGlobalMapping(gv, s.address);
                    }
                    gv = (llvm::Function *)m->getOrInsertFunction(name, (FunctionType *)copy_llvm_type_to_context(m->getContext(), s.llvm_type));
                } else {
                    gv = (GlobalValue *)m->getOrInsertGlobal(name, copy_llvm_type_to_context(m->getContext(), s.llvm_type));
                }
                ee->addGlobalMapping(gv, s.address);
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
        wrapper_fn = (int (*)(const void **))temp.address;

    }

    for (size_t i = 0; i < requested_exports.size(); i++) {
        exports[requested_exports[i]] = compile_and_get_function(ee, m, requested_exports[i]);
    }

    debug(2) << "Finalizing object\n";
    ee->finalizeObject();

    // Stash the various objects that need to stay alive behind a reference-counted pointer.
    jit_module = new JITModuleContents(exports, ee, m, main_fn, wrapper_fn);

    // Do any target-specific post-compilation module meddling
    cg->jit_finalize(ee, m, /* &jit_module.ptr->cleanup_routines */ NULL); // TODO: fix this

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

const std::map<std::string, JITModule::Symbol> &JITModule::Exports() const {
    return jit_module.ptr->exports;
}

void JITModule::make_externs(llvm::Module *module) {
    const std::map<std::string, Symbol> &dep_exports(Exports());
    std::map<std::string, Symbol>::const_iterator iter;
    for (iter = dep_exports.begin(); iter != dep_exports.end(); iter++) {
        const std::string &name(iter->first);
        const Symbol &s(iter->second);
        //      debug(0) << "Export " << name << " has type " << s.llvm_type->getTypeID() << "\n";
        GlobalValue *gv;
        if (s.llvm_type->isFunctionTy()) {
          //      debug(0) << "Defining extern function " << name << "\n";
            gv = (llvm::Function *)module->getOrInsertFunction(name, (FunctionType *)copy_llvm_type_to_context(module->getContext(), s.llvm_type));
        } else {
          //      debug(0) << "Defining extern variable " << name << "\n";
            gv = (GlobalValue *)module->getOrInsertGlobal(name, copy_llvm_type_to_context(module->getContext(), s.llvm_type));
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

bool JITSharedRuntime::inited = false;
llvm::LLVMContext *host_shared_jit_llvm_context = NULL;
JITModule JITSharedRuntime::host_shared_jit_runtime;

JITModule &JITSharedRuntime::Get(CodeGen *cg) {
    // TODO: Thread safety
    if (!inited) {
        host_shared_jit_llvm_context = new LLVMContext();
        llvm::Module *shared_runtime = get_initial_module_for_target(get_jit_target_from_environment(),
                                                                     host_shared_jit_llvm_context, true);

        std::set<std::string> halide_exports_unique;

        // Enumerate the global variables.
        for (Module::const_global_iterator iter = shared_runtime->global_begin(); iter != shared_runtime->global_end(); iter++) {
            if (const GlobalValue *gv = dyn_cast<GlobalValue>(iter)) {
                if (gv->hasWeakLinkage() && starts_with(gv->getName(), "halide_")) {
                  //              debug(0) << "Found global " << (std::string)gv->getName() << " with type " << gv->getType()->getTypeID() << "\n";
                    halide_exports_unique.insert(gv->getName());
                }
            }
        }

        // Enumerate the functions.
        for (Module::const_iterator iter = shared_runtime->begin(); iter != shared_runtime->end(); iter++) {
            const llvm::Function *gv = cast<llvm::Function>(iter);
            if (gv->hasWeakLinkage() && starts_with(gv->getName(), "halide_")) {
                debug(0) << "Found function " << (std::string)gv->getName() << " with type " << gv->getType()->getTypeID() << " function type is " << gv->getFunctionType()->getTypeID() << "\n";
                halide_exports_unique.insert(gv->getName());
            }
        }

        std::vector<std::string> halide_exports(halide_exports_unique.begin(), halide_exports_unique.end());
        host_shared_jit_runtime.compile_module(cg, shared_runtime, "", std::vector<JITModule>(), halide_exports);

        inited = true;
    }
    return host_shared_jit_runtime;
}

}
}
