#ifndef HALIDE_RUNTIME_GPU_CONTEXT_COMMON_H_
#define HALIDE_RUNTIME_GPU_CONTEXT_COMMON_H_

#include "HalideRuntime.h"
#include "printer.h"
#include "scoped_mutex_lock.h"

namespace Halide {
namespace Internal {

template<typename ContextT, typename ModuleStateT>
class GPUCompilationCache {
    struct CachedCompilation {
        ContextT context{};
        ModuleStateT module_state{};
        uintptr_t kernel_id{0};
        uintptr_t use_count{0};
    };

    halide_mutex mutex;

    static constexpr float kLoadFactor{.5f};
    static constexpr int kInitialTableBits{7};
    int log2_compilations_size{0};  // number of bits in index into compilations table.
    CachedCompilation *compilations{nullptr};
    int count{0};

    static constexpr uintptr_t kInvalidId{0};
    static constexpr uintptr_t kDeletedId{1};

    uintptr_t unique_id{2};  // zero is an invalid id

    static ALWAYS_INLINE uintptr_t kernel_hash(ContextT context, uintptr_t id, int bits) {
        uintptr_t addr = (uintptr_t)context + id;
        // Fibonacci hashing. The golden ratio is 1.9E3779B97F4A7C15F39...
        // in hexadecimal.
        if constexpr (sizeof(uintptr_t) >= 8) {
            return (addr * (uintptr_t)0x9E3779B97F4A7C15) >> (64 - bits);
        } else {
            return (addr * (uintptr_t)0x9E3779B9) >> (32 - bits);
        }
    }

    HALIDE_MUST_USE_RESULT bool insert(const CachedCompilation &entry) {
        if (log2_compilations_size == 0) {
            if (!resize_table(kInitialTableBits)) {
                return false;
            }
        }
        if ((count + 1) > (1 << log2_compilations_size) * kLoadFactor) {
            if (!resize_table(log2_compilations_size + 1)) {
                return false;
            }
        }
        count += 1;
        uintptr_t index = kernel_hash(entry.context, entry.kernel_id, log2_compilations_size);
        for (int i = 0; i < (1 << log2_compilations_size); i++) {
            uintptr_t effective_index = (index + i) & ((1 << log2_compilations_size) - 1);
            if (compilations[effective_index].kernel_id <= kDeletedId) {
                compilations[effective_index] = entry;
                return true;
            }
        }
        // This is a logic error that should never occur. It means the table is
        // full, but it should have been resized.
        halide_debug_assert(nullptr, false);
        return false;
    }

    HALIDE_MUST_USE_RESULT bool find_internal(ContextT context, uintptr_t id,
                                              ModuleStateT *&module_state, int increment) {
        if (log2_compilations_size == 0) {
            return false;
        }
        uintptr_t index = kernel_hash(context, id, log2_compilations_size);
        for (int i = 0; i < (1 << log2_compilations_size); i++) {
            uintptr_t effective_index = (index + i) & ((1 << log2_compilations_size) - 1);

            if (compilations[effective_index].kernel_id == kInvalidId) {
                return false;
            }
            if (compilations[effective_index].context == context &&
                compilations[effective_index].kernel_id == id) {
                module_state = &compilations[effective_index].module_state;
                if (increment != 0) {
                    compilations[effective_index].use_count += increment;
                }
                return true;
            }
        }
        return false;
    }

    HALIDE_MUST_USE_RESULT bool resize_table(int size_bits) {
        if (size_bits != log2_compilations_size) {
            int new_size = (1 << size_bits);
            int old_size = (1 << log2_compilations_size);
            CachedCompilation *new_table = (CachedCompilation *)malloc(new_size * sizeof(CachedCompilation));
            if (new_table == nullptr) {
                // signal error.
                return false;
            }
            memset(new_table, 0, new_size * sizeof(CachedCompilation));
            CachedCompilation *old_table = compilations;
            compilations = new_table;
            log2_compilations_size = size_bits;

            if (count > 0) {  // Mainly to catch empty initial table case
                for (int32_t i = 0; i < old_size; i++) {
                    if (old_table[i].kernel_id != kInvalidId &&
                        old_table[i].kernel_id != kDeletedId) {
                        bool result = insert(old_table[i]);
                        halide_debug_assert(nullptr, result);  // Resizing the table while resizing the table is a logic error.
                        (void)result;
                    }
                }
            }
            free(old_table);
        }
        return true;
    }

    template<typename FreeModuleT>
    void release_context_already_locked(void *user_context, bool all, ContextT context, FreeModuleT &f) {
        if (count == 0) {
            return;
        }

        for (int i = 0; i < (1 << log2_compilations_size); i++) {
            if (compilations[i].kernel_id > kInvalidId &&
                (all || (compilations[i].context == context)) &&
                compilations[i].use_count == 0) {
                debug(user_context) << "Releasing cached compilation: " << compilations[i].module_state
                                    << " id " << compilations[i].kernel_id
                                    << " context " << compilations[i].context << "\n";
                f(compilations[i].module_state);
                compilations[i].module_state = nullptr;
                compilations[i].kernel_id = kDeletedId;
                count--;
            }
        }
    }

public:
    HALIDE_MUST_USE_RESULT bool lookup(ContextT context, void *state_ptr, ModuleStateT &module_state) {
        ScopedMutexLock lock_guard(&mutex);

        uintptr_t id = (uintptr_t)state_ptr;
        ModuleStateT *mod_ptr;
        if (find_internal(context, id, mod_ptr, 0)) {
            module_state = *mod_ptr;
            return true;
        }
        return false;
    }

    template<typename FreeModuleT>
    void release_context(void *user_context, bool all, ContextT context, FreeModuleT &f) {
        ScopedMutexLock lock_guard(&mutex);

        release_context_already_locked(user_context, all, context, f);
    }

    template<typename FreeModuleT>
    void delete_context(void *user_context, ContextT context, FreeModuleT &f) {
        ScopedMutexLock lock_guard(&mutex);

        release_context_already_locked(user_context, false, context, f);
    }

    template<typename FreeModuleT>
    void release_all(void *user_context, FreeModuleT &f) {
        ScopedMutexLock lock_guard(&mutex);

        release_context_already_locked(user_context, true, nullptr, f);
        // Some items may have been in use, so can't free.
        if (count == 0) {
            free(compilations);
            compilations = nullptr;
            log2_compilations_size = 0;
        }
    }

    template<typename CompileModuleT, typename... Args>
    HALIDE_MUST_USE_RESULT bool kernel_state_setup(void *user_context, void **state_ptr_ptr,
                                                   ContextT context, ModuleStateT &result,
                                                   CompileModuleT f,
                                                   Args... args) {
        ScopedMutexLock lock_guard(&mutex);

        uintptr_t *id_ptr = (uintptr_t *)state_ptr_ptr;
        if (*id_ptr == 0) {
            *id_ptr = unique_id++;
            if (unique_id == (uintptr_t)-1) {
                // Sorry, out of ids
                return false;
            }
        }

        ModuleStateT *mod;
        if (find_internal(context, *id_ptr, mod, 1)) {
            result = *mod;
            return true;
        }

        // TODO(zvookin): figure out the calling signature here...
        ModuleStateT compiled_module = f(args...);
        debug(user_context) << "Caching compiled kernel: " << compiled_module
                            << " id " << *id_ptr << " context " << context << "\n";
        if (compiled_module == nullptr) {
            return false;
        }

        if (!insert({context, compiled_module, *id_ptr, 1})) {
            return false;
        }
        result = compiled_module;

        return true;
    }

    void release_hold(void *user_context, ContextT context, void *state_ptr) {
        ScopedMutexLock lock_guard(&mutex);

        ModuleStateT *mod;
        uintptr_t id = (uintptr_t)state_ptr;
        bool result = find_internal(context, id, mod, -1);
        halide_debug_assert(user_context, result);  // Value must be in cache to be released
        (void)result;
    }
};

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_RUNTIME_GPU_CONTEXT_COMMON_H_
