#ifndef HALIDE_JIT_MODULE_H
#define HALIDE_JIT_MODULE_H

/** \file
 * Defines the struct representing lifetime and dependencies of
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

// TODO: Consider moving these two types elsewhere and seeing if they
// can be combined with other types used for argument handling, or used
// elsewhere.
struct ScalarOrBufferT {
    bool is_buffer;
    Type scalar_type; // Only meaningful if is_buffer is false.
    ScalarOrBufferT() : is_buffer(false) { }
};

struct ExternSignature {
    bool is_void_return;
    Type ret_type;
    std::vector<ScalarOrBufferT> arg_types;

    ExternSignature() : is_void_return(false) { }
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
     * arbitrary pointers to functions or global variables to JITted
     * code. */
    EXPORT const std::map<std::string, Symbol> &exports() const;

    /** A pointer to the raw halide function. Its true type depends
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
    /** Registers a single function as available to modules which
     * depend on this one. This routine converts the ExternSignature
     * info into an LLVM type, which allows type safe linkage of
     * external routines. */
    EXPORT void add_extern_for_export(const std::string &name,
                                      const ExternSignature &signature, void *address);

    /** Look up a symbol by name in this module or its dependencies. */
    EXPORT Symbol find_symbol_by_name(const std::string &) const;

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
