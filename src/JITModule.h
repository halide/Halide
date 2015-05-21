#ifndef HALIDE_JIT_MODULE_H
#define HALIDE_JIT_MODULE_H

/** \file Defines the struct representing lifetime and dependencies of
 * a JIT compiled halide pipeline
 */

#include <map>

#include "IntrusivePtr.h"
#include "Type.h"
#include "runtime/HalideRuntime.h"

namespace llvm {
class Module;
class Type;
}

namespace Halide {

struct Target;
class Module;

class Func;

struct ScalarOrBufferT {
    bool is_buffer;
    Type scalar_type; // Only meaningful if is_buffer is false.
};

namespace {

template <typename T>
bool voidable_halide_type(Type &t) {
    t = type_of<T>();
    return false;
}

template<>
inline bool voidable_halide_type<void>(Type &t) {
    return true;
}        

template <typename T>
bool scalar_arg_type_or_buffer(Type &t) {
    t = type_of<T>();
    return false;
}

template <>
inline bool scalar_arg_type_or_buffer<struct buffer_t *>(Type &t) {
    return true;
}

template <typename T>
ScalarOrBufferT arg_type_info() {
    ScalarOrBufferT result;
    result.is_buffer = scalar_arg_type_or_buffer<T>(result.scalar_type);
    return result;
}

template <typename A1, typename... Args>
struct make_argument_list {
    static void add_args(std::vector<ScalarOrBufferT> &arg_types) {
        arg_types.push_back(arg_type_info<A1>());
        make_argument_list<Args...>::add_args(arg_types);
    }
};

template <>
struct make_argument_list<void> {
    static void add_args(std::vector<ScalarOrBufferT> &) { }
};


template <typename... Args>
void init_arg_types(std::vector<ScalarOrBufferT> &arg_types) {
  make_argument_list<Args..., void>::add_args(arg_types);
}

}

struct JITExtern {
    // assert func.defined() == (c_function == NULL) -- strictly one or the other
    Func *func;
    std::map<std::string, JITExtern> func_externs;

    void *c_function;
    bool is_void_return; // could use ret_type.bits == 0...
    Type ret_type;
    std::vector<ScalarOrBufferT> arg_types;

   JITExtern(Func &func,
             const std::map<std::string, JITExtern> &func_externs = std::map<std::string, JITExtern>())
        : func(&func), func_externs(func_externs), c_function(NULL) {
    }

    template <typename RT, typename... Args>
    JITExtern(RT (*f)(Args... args)) : func(NULL) {
        c_function = (void *)f;
        is_void_return = voidable_halide_type<RT>(ret_type);
        init_arg_types<Args...>(arg_types);
    }
};

namespace Internal {

class JITModuleContents;
struct LoweredFunc;

struct JITModule {
    IntrusivePtr<JITModuleContents> jit_module;

    struct Symbol {
        void *address;
        llvm::Type *llvm_type;
        Symbol() : address(NULL), llvm_type(NULL) {}
        Symbol(void *address, llvm::Type *llvm_type) : address(address), llvm_type(llvm_type) {}
    };

    EXPORT JITModule();
    EXPORT JITModule(const Module &m, const LoweredFunc &fn,
                     const std::vector<JITModule> &dependencies = std::vector<JITModule>());
    /** The exports map of a JITModule contains all symbols which are
     * available to other JITModules which depend on this one. For
     * runtime modules, this is all of the symbols exported from the
     * runtime. For a JITted Func, it generally only contains the main
     * result Func of the compilation, which takes its name directly
     * from the Func declaration. One can also make a module which
     * contains no code itself but is just an exports maps providing
     * arbitarty pointers to functions or global variables to JITted
     * code. */
    EXPORT const std::map<std::string, Symbol> &exports() const;

    /** A pointer to the raw halide function. It's true type depends
     * on the Argument vector passed to CodeGen_LLVM::compile. Image
     * parameters become (buffer_t *), and scalar parameters become
     * pointers to the appropriate values. The final argument is a
     * pointer to the buffer_t defining the output. This will be NULL for
     * a JITModule which has not yet been compiled or one that is not
     * a Halide Func compilation at all. */
    EXPORT void *main_function() const;

    /** Returns the Symbol structure for the routine documented in
     * main_function. Returning a Symbol allows access to the LLVM
     * type as well as the address. The address and type will be NULL
     * if the module has not been compiled. */
    EXPORT Symbol entrypoint_symbol() const;

    /** Returns the Symbol structure for the argv wrapper routine
     * corresponding to the entrypoint. The argv wrapper is callable
     * via an array of void * pointers to the arguments for the
     * call. Returning a Symbol allows access to the LLVM type as well
     * as the address. The address and type will be NULL if the module
     * has not been compiled. */
    EXPORT Symbol argv_entrypoint_symbol() const;

    /** A slightly more type-safe wrapper around the raw halide
     * module. Takes it arguments as an array of pointers that
     * correspond to the arguments to \ref main_function . This will
     * be NULL for a JITModule which has not yet been compiled or one
     * that is not a Halide Func compilation at all. */
    // @{
    typedef int (*argv_wrapper)(const void **args);
    EXPORT argv_wrapper argv_function() const;
    // @}

    /** Add another JITModule to the dependency chain. Dependencies
     * are searched to resolve symbols not found in the current
     * compilation unit while JITting. */
    EXPORT void add_dependency(JITModule &dep);
    /** Registers a single Symbol as available to modules which depend
     * on this one. The Symbol structure provides both the address and
     * the LLVM type for the function, which allows type safe linkage of
     * extenal routines. */
    EXPORT void add_symbol_for_export(const std::string &name, const Symbol &extern_symbol);
    /** Registers a single JITExtern as available to modules which
     * depend on this one. The JITExtern must be of the C function
     * variety and not a Func. This routine converts the Halide type
     * info into an LLVM type, which allows type safe linkage of
     * extenal routines. */
    EXPORT void add_extern_for_export(const std::string &name, const JITExtern &jit_extern);

    /** Take an llvm module and compile it. The requested exports will
        be available via the exports method. */
    EXPORT void compile_module(llvm::Module *mod, const std::string &function_name, const Target &target,
                               const std::vector<JITModule> &dependencies = std::vector<JITModule>(),
                               const std::vector<std::string> &requested_exports = std::vector<std::string>());

    /** Make extern declarations for all exports of a set of JITModules in another llvm::Module */
    EXPORT static void make_externs(const std::vector<JITModule> &deps, llvm::Module *mod);

    /** Encapsulate device (GPU) and buffer interactions. */
    EXPORT int copy_to_device(struct buffer_t *buf) const;
    EXPORT int copy_to_host(struct buffer_t *buf) const;
    EXPORT int device_free(struct buffer_t *buf) const;
    EXPORT void memoization_cache_set_size(int64_t size) const;

    /** Return true if compile_module has been called on this module. */
    EXPORT bool compiled() const;
};

typedef int (*halide_task)(void *user_context, int, uint8_t *);

struct JITHandlers {
    void (*custom_print)(void *, const char *);
    void *(*custom_malloc)(void *, size_t);
    void (*custom_free)(void *, void *);
    int (*custom_do_task)(void *, halide_task, int, uint8_t *);
    int (*custom_do_par_for)(void *, halide_task, int, int, uint8_t *);
    void (*custom_error)(void *, const char *);
    int32_t (*custom_trace)(void *, const halide_trace_event *);
    JITHandlers() : custom_print(NULL), custom_malloc(NULL), custom_free(NULL),
                    custom_do_task(NULL), custom_do_par_for(NULL),
                    custom_error(NULL), custom_trace(NULL) {
    }
};

struct JITUserContext {
    void *user_context;
    JITHandlers handlers;
};

class JITSharedRuntime {
public:
    // Note only the first llvm::Module passed in here is used. The same shared runtime is used for all JIT.
    EXPORT static std::vector<JITModule> get(llvm::Module *m, const Target &target, bool create = true);
    EXPORT static void init_jit_user_context(JITUserContext &jit_user_context, void *user_context, const JITHandlers &handlers);
    EXPORT static JITHandlers set_default_handlers(const JITHandlers &handlers);

    /** Set the maximum number of bytes used by memoization caching.
     * If you are compiling statically, you should include HalideRuntime.h
     * and call halide_memoization_cache_set_size() instead.
     */
    EXPORT static void memoization_cache_set_size(int64_t size);

    EXPORT static void release_all();
};

}
}

#endif
