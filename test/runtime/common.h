#ifndef HALIDE_TEST_RUNTIME_COMMON_H_
#define HALIDE_TEST_RUNTIME_COMMON_H_

#include "HalideRuntime.h"

namespace Halide::Runtime::Internal {

size_t get_allocated_system_memory();
void *allocate_system(void *user_context, size_t bytes);
void deallocate_system(void *user_context, void *aligned_ptr);

}  // namespace Halide::Runtime::Internal

#define HALIDE_CHECK(user_context, cond)                                                                                           \
    do {                                                                                                                           \
        if (!(cond)) {                                                                                                             \
            halide_print(user_context, __FILE__ ":" _halide_expand_and_stringify(__LINE__) " HALIDE_CHECK() failed: " #cond "\n"); \
            abort();                                                                                                               \
        }                                                                                                                          \
    } while (0)

#endif  // HALIDE_TEST_RUNTIME_COMMON_H_
