#ifndef HALIDE_JIT_MODULE_H
#define HALIDE_JIT_MODULE_H

/** \file
 * Defines the struct representing lifetime and dependencies of
 * a JIT compiled halide pipeline
 */

#include <map>
#include <memory>

#include "IntrusivePtr.h"
#include "Type.h"
#include "runtime/HalideRuntime.h"

namespace llvm {
class Module;
class Type;
}

namespace Halide {

struct ExternCFunction;
struct JITExtern;
struct Target;
class Module;

namespace Internal {

class JITModuleContents;
struct LoweredFunc;

struct JITModule {
    IntrusivePtr<JITModuleContents> jit_module;

    struct Symbol {
        void *address = nullptr;
        Symbol() : address(nullptr) {}
        explicit Symbol(void *address) : address(address) {}
    };

    JITModule();
    JITModule(const Module &m, const LoweredFunc &fn,
                     const std::vector<JITModule> &dependencies = std::vector<JITModule>());

    /** Take a list of JITExterns and generate trampoline functions
     * which can be called dynamically via a function pointer that
     * takes an array of void *'s for each argument and the return
     * value.
     */
    static JITModule make_trampolines_module(const Target &target,
                                             const std::map<std::string, JITExtern> &externs,
                                             const std::string &suffix,
                                             const std::vector<JITModule> &deps);

    /** The exports map of a JITModule contains all symbols which are
     * available to other JITModules which depend on this one. For
     * runtime modules, this is all of the symbols exported from the
     * runtime. For a JITted Func, it generally only contains the main
     * result Func of the compilation, which takes its name directly
     * from the Func declaration. One can also make a module which
     * contains no code itself but is just an exports maps providing
     * arbitrary pointers to functions or global variables to JITted
     * code. */
    const std::map<std::string, Symbol> &exports() const;

    /** A pointer to the raw halide function. Its true type depends
     * on the Argument vector passed to CodeGen_LLVM::compile. Image
     * parameters become (halide_buffer_t *), and scalar parameters become
     * pointers to the appropriate values. The final argument is a
     * pointer to the halide_buffer_t defining the output. This will be nullptr for
     * a JITModule which has not yet been compiled or one that is not
     * a Halide Func compilation at all. */
    void *main_function() const;

    /** Returns the Symbol structure for the routine documented in
     * main_function. Returning a Symbol allows access to the LLVM
     * type as well as the address. The address and type will be nullptr
     * if the module has not been compiled. */
    Symbol entrypoint_symbol() const;

    /** Returns the Symbol structure for the argv wrapper routine
     * corresponding to the entrypoint. The argv wrapper is callable
     * via an array of void * pointers to the arguments for the
     * call. Returning a Symbol allows access to the LLVM type as well
     * as the address. The address and type will be nullptr if the module
     * has not been compiled. */
    Symbol argv_entrypoint_symbol() const;

    /** A slightly more type-safe wrapper around the raw halide
     * module. Takes it arguments as an array of pointers that
     * correspond to the arguments to \ref main_function . This will
     * be nullptr for a JITModule which has not yet been compiled or one
     * that is not a Halide Func compilation at all. */
    // @{
    typedef int (*argv_wrapper)(const void **args);
    argv_wrapper argv_function() const;
    // @}

    /** Add another JITModule to the dependency chain. Dependencies
     * are searched to resolve symbols not found in the current
     * compilation unit while JITting. */
    void add_dependency(JITModule &dep);
    /** Registers a single Symbol as available to modules which depend
     * on this one. The Symbol structure provides both the address and
     * the LLVM type for the function, which allows type safe linkage of
     * extenal routines. */
    void add_symbol_for_export(const std::string &name, const Symbol &extern_symbol);
    /** Registers a single function as available to modules which
     * depend on this one. This routine converts the ExternSignature
     * info into an LLVM type, which allows type safe linkage of
     * external routines. */
    Symbol add_extern_for_export(const std::string &name,
                                 const ExternCFunction &extern_c_function);

    /** Look up a symbol by name in this module or its dependencies. */
    Symbol find_symbol_by_name(const std::string &) const;

    /** Take an llvm module and compile it. The requested exports will
        be available via the exports method. */
    void compile_module(std::unique_ptr<llvm::Module> mod,
                        const std::string &function_name, const Target &target,
                        const std::vector<JITModule> &dependencies = std::vector<JITModule>(),
                        const std::vector<std::string> &requested_exports = std::vector<std::string>());

    /** Encapsulate device (GPU) and buffer interactions. */
    void memoization_cache_set_size(int64_t size) const;

    /** Return true if compile_module has been called on this module. */
    bool compiled() const;
};

typedef int (*halide_task)(void *user_context, int, uint8_t *);

struct JITHandlers {
    void (*custom_print)(void *, const char *){nullptr};
    void *(*custom_malloc)(void *, size_t){nullptr};
    void (*custom_free)(void *, void *){nullptr};
    int (*custom_do_task)(void *, halide_task, int, uint8_t *){nullptr};
    int (*custom_do_par_for)(void *, halide_task, int, int, uint8_t *){nullptr};
    void (*custom_error)(void *, const char *){nullptr};
    int32_t (*custom_trace)(void *, const halide_trace_event_t *){nullptr};
    void *(*custom_get_symbol)(const char *name){nullptr};
    void *(*custom_load_library)(const char *name){nullptr};
    void *(*custom_get_library_symbol)(void *lib, const char *name){nullptr};
};

struct JITUserContext {
    void *user_context;
    JITHandlers handlers;
};

class JITSharedRuntime {
public:
    // Note only the first llvm::Module passed in here is used. The same shared runtime is used for all JIT.
    static std::vector<JITModule> get(llvm::Module *m, const Target &target, bool create = true);
    static void init_jit_user_context(JITUserContext &jit_user_context, void *user_context, const JITHandlers &handlers);
    static JITHandlers set_default_handlers(const JITHandlers &handlers);

    /** Set the maximum number of bytes used by memoization caching.
     * If you are compiling statically, you should include HalideRuntime.h
     * and call halide_memoization_cache_set_size() instead.
     */
    static void memoization_cache_set_size(int64_t size);

    static void release_all();
};

void *get_symbol_address(const char *s);

}  // namespace Internal
}  // namespace Halide

#endif
