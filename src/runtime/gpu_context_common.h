#include "printer.h"
#include "scoped_mutex_lock.h"

namespace Halide { namespace Internal {
  
template <typename ContextT, typename ModuleStateT>
struct GPUCompilationCache {
    struct CachedCompilation {
        ContextT context;
        ModuleStateT module_state;
        uint32_t kernel_id;
    };

    halide_mutex mutex;

    static constexpr float kLoadFactor = .5f;
    static constexpr int kInitialTableBits = 7;
    int log2_compilations_size{0}; // number of bits in index into compilations table.
    CachedCompilation *compilations{nullptr};
    int count{0};

    static constexpr uint32_t kInvalidId{0};
    static constexpr uint32_t kDeletedId{1};

    uint32_t unique_id{2}; // zero is an invalid id
  
    static ALWAYS_INLINE uintptr_t kernel_hash(ContextT context, uint32_t id, uint32_t bits) {
        uintptr_t addr = (uintptr_t)context + id;
        // Fibonacci hashing. The golden ratio is 1.9E3779B97F4A7C15F39...
        // in hexadecimal.
        if (sizeof(uintptr_t) >= 8) {
            return (addr * (uintptr_t)0x9E3779B97F4A7C15) >> (64 - bits);
        } else {
            return (addr * (uintptr_t)0x9E3779B9) >> (32 - bits);
        }
    }

    bool insert(ContextT context, uint32_t id, ModuleStateT module_state) {
        if (log2_compilations_size == 0) {
            if (!resize_table(kInitialTableBits)) {
                return false;
            }
        }
        if (count++ > (1 << log2_compilations_size) * kLoadFactor) {
            if (!resize_table(log2_compilations_size + 1)) {
                return false;
            }
        }
        uintptr_t index = kernel_hash(context, id, log2_compilations_size);
        for (int i = 0; i < (1 << log2_compilations_size); i++) {
            uintptr_t effective_index = (index + i) & ((1 << log2_compilations_size) - 1);
            if (compilations[effective_index].kernel_id <= kDeletedId) {
                compilations[effective_index].context = context;
                compilations[effective_index].module_state = module_state;
                compilations[effective_index].kernel_id = id;
                return true;
            }
        }
        // This is a logic error that should never occur. It means the table is
        // full, but it should have been resized.
        halide_assert(nullptr, false);
        return false;
    }

    bool find_internal(ContextT context, uint32_t id, ModuleStateT *&module_state) {
        if (log2_compilations_size == 0) {
            return false;
        }
        uintptr_t index = kernel_hash(context, id, log2_compilations_size);
        for (int i = 0; i < (1 << log2_compilations_size); i++) {
            uintptr_t effective_index = (index + i) & ((1 << log2_compilations_size) - 1);
            if (compilations[effective_index].context == context &&
                compilations[effective_index].kernel_id == id) {
                module_state = &compilations[effective_index].module_state;
                return true;
            }
        }
        return false;
    }

    bool lookup(ContextT context, void *state_ptr, ModuleStateT &module_state) {
        ScopedMutexLock lock_guard(&mutex);
        uint32_t id = (uint32_t)(uintptr_t)state_ptr;
        ModuleStateT *mod_ptr;
        if (find_internal(context, id, mod_ptr)) {
            module_state = *mod_ptr;
            return true;
        }
        return false;
    }
 
    bool resize_table(int size_bits) {
        if (size_bits != log2_compilations_size) {
            int new_size = (1 << size_bits);
            int old_size = (1 << log2_compilations_size);
            CachedCompilation *new_table = (CachedCompilation *)malloc(new_size * sizeof(CachedCompilation));
            if (new_table == nullptr) {
                // signal error.
                return false;
            }
            memset(new_table, 0, (1 << size_bits) * sizeof(CachedCompilation));
            CachedCompilation *old_table = compilations;
            compilations = new_table;
            log2_compilations_size = size_bits;
 
            if (count > 0) { // MAinly to catch empty initial table case
                for (int32_t i = 0; i < old_size; i++) {
                    if (old_table[i].kernel_id != kInvalidId &&
                        old_table[i].kernel_id != kDeletedId) {
                        insert(old_table[i].context, old_table[i].kernel_id, old_table[i].module_state);
                    }
                }
            }
            free(old_table);
        }
        return true;
    }

    template <typename FreeModuleT>
    void release_context(void *user_context, bool all, ContextT context, FreeModuleT &f) {
        if (count == 0) {
            return;
        }

        for (int i = 0; i < (1 << log2_compilations_size); i++) {
            if (compilations[i].kernel_id > kInvalidId &&
                (all || compilations[i].context == context)) {
                debug(user_context) << "Releasing cached compilation: " << compilations[i].module_state << "\n";
                f(compilations[i].module_state);
                compilations[i].module_state = nullptr;
                compilations[i].kernel_id = kInvalidId;
                count--;
            }
        }
    }

    template <typename FreeModuleT>
    void delete_context(void *user_context, ContextT context, FreeModuleT &f) {
        ScopedMutexLock lock_guard(&mutex);

        release_context(user_context, false, context, f);
    }

    template <typename FreeModuleT>
    void release_all(void *user_context, FreeModuleT &f) {
        ScopedMutexLock lock_guard(&mutex);

        release_context(user_context, true, nullptr, f);
        free(compilations);
        compilations = nullptr;
        log2_compilations_size = 0;
    }

    template <typename CompileModuleT, typename... Args>
    bool kernel_state_setup(void * user_context, void **state_ptr,
                            ContextT context, ModuleStateT &result,
                            CompileModuleT f,
                            Args... args) {
        ScopedMutexLock lock_guard(&mutex);

        uint32_t *id_ptr = (uint32_t *)state_ptr;
        if (*id_ptr == 0) {
            *id_ptr = unique_id++;
        }

        ModuleStateT *mod;
        if (find_internal(context, *id_ptr, mod)) {
            result = *mod;
            return true;
        }

        // TODO(zvookin): figure out the calling signature here...
        ModuleStateT compiled_module = f(args...);
        debug(user_context) << "Caching compiled kernel: " << compiled_module << "\n";
        if (compiled_module == nullptr) {
            return false;
        }

        insert(context, *id_ptr, compiled_module);
        result = compiled_module;
        
        return true;
    }

};

} }
