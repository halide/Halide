#include "HalideRuntime.h"

#include "printer.h"

extern "C" {

extern int printf(const char *format, ...);

void halide_print(void *user_context, const char *str) {
    printf("%s\n", str);
}

void halide_error(void *user_context, const char *msg) {
    halide_print(user_context, msg);
    abort();
}

void halide_profiler_report(void *user_context) {
}

void halide_profiler_reset() {
}

}  // extern "C"

namespace {

size_t allocated_system_memory = 0;

void *align_up(void *ptr, size_t offset, size_t alignment) {
    return (void *)(((((size_t)ptr + offset)) + (alignment - 1)) & ~(alignment - 1));
}

}  // namespace

namespace Halide::Runtime::Internal {

size_t get_allocated_system_memory() {
    return allocated_system_memory;
}

void *allocate_system(void *user_context, size_t bytes) {
    constexpr size_t alignment = 128;
    constexpr size_t header_size = 2 * sizeof(size_t);
    size_t alloc_size = bytes + header_size + (alignment - 1);
    void *raw_ptr = malloc(alloc_size);
    if (raw_ptr == nullptr) {
        return nullptr;
    }
    void *aligned_ptr = align_up(raw_ptr, header_size, alignment);
    size_t aligned_offset = (size_t)((size_t)aligned_ptr - (size_t)raw_ptr);
    *((size_t *)aligned_ptr - 1) = aligned_offset;
    *((size_t *)aligned_ptr - 2) = alloc_size;
    allocated_system_memory += alloc_size;

    debug(user_context) << "Test : allocate_system ("
                        << "ptr=" << (void *)(raw_ptr) << " "
                        << "aligned_ptr=" << (void *)(aligned_ptr) << " "
                        << "aligned_offset=" << int32_t(aligned_offset) << " "
                        << "alloc_size=" << int32_t(alloc_size) << " "
                        << "allocated_system_memory=" << int32_t(get_allocated_system_memory()) << " "
                        << ") !\n";

    return aligned_ptr;
}

void deallocate_system(void *user_context, void *aligned_ptr) {
    size_t aligned_offset = *((size_t *)aligned_ptr - 1);
    size_t alloc_size = *((size_t *)aligned_ptr - 2);
    void *raw_ptr = (void *)((uint8_t *)aligned_ptr - aligned_offset);
    // The compiler may see printing the pointer value after the free as a use.
    // This protects against a use after free warning.
    intptr_t raw_ptr_save = (intptr_t)raw_ptr;
    free(raw_ptr);
    allocated_system_memory -= alloc_size;

    debug(user_context) << "Test : deallocate_system ("
                        << "ptr=" << (void *)(raw_ptr_save) << " "
                        << "aligned_ptr=" << (void *)(aligned_ptr) << " "
                        << "aligned_offset=" << int32_t(aligned_offset) << " "
                        << "alloc_size=" << int32_t(alloc_size) << " "
                        << "allocated_system_memory=" << int32_t(get_allocated_system_memory()) << " "
                        << ") !\n";
}

}  // namespace Halide::Runtime::Internal
