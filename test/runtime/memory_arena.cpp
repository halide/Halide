#include "HalideRuntime.h"

#include "common.h"
#include "printer.h"

#include "internal/memory_arena.h"

using namespace Halide::Runtime::Internal;

struct TestStruct {
    int8_t i8;
    uint16_t ui16;
    float f32;
};

int main(int argc, char **argv) {
    void *user_context = (void *)1;

    // test class interface
    {
        SystemMemoryAllocatorFns test_allocator = {allocate_system, deallocate_system};

        MemoryArena::Config config = {sizeof(int), 32, 0};
        MemoryArena arena(user_context, config, test_allocator);
        void *p1 = arena.reserve(user_context);
        HALIDE_CHECK(user_context, get_allocated_system_memory() >= (1 * sizeof(int)));
        HALIDE_CHECK(user_context, p1 != nullptr);

        void *p2 = arena.reserve(user_context, true);
        HALIDE_CHECK(user_context, get_allocated_system_memory() >= (2 * sizeof(int)));
        HALIDE_CHECK(user_context, p2 != nullptr);
        HALIDE_CHECK(user_context, (*static_cast<int *>(p2)) == 0);

        arena.reclaim(user_context, p1);
        arena.destroy(user_context);

        HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);
    }

    // test dyanmic construction
    {
        SystemMemoryAllocatorFns test_allocator = {allocate_system, deallocate_system};

        MemoryArena::Config config = {sizeof(double), 32, 0};
        MemoryArena *arena = MemoryArena::create(user_context, config, test_allocator);

        constexpr size_t count = 4 * 1024;
        void *pointers[count];
        for (size_t n = 0; n < count; ++n) {
            pointers[n] = arena->reserve(user_context, true);
        }
        HALIDE_CHECK(user_context, get_allocated_system_memory() >= (count * sizeof(int)));
        for (size_t n = 0; n < count; ++n) {
            void *ptr = pointers[n];
            HALIDE_CHECK(user_context, ptr != nullptr);
            HALIDE_CHECK(user_context, (*static_cast<double *>(ptr)) == 0.0);
        }
        arena->destroy(user_context);

        MemoryArena::destroy(user_context, arena);
        HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);
    }

    // test struct allocations
    {
        SystemMemoryAllocatorFns test_allocator = {allocate_system, deallocate_system};
        MemoryArena::Config config = {sizeof(TestStruct), 32, 0};
        MemoryArena arena(user_context, config, test_allocator);
        void *s1 = arena.reserve(user_context, true);
        HALIDE_CHECK(user_context, s1 != nullptr);
        HALIDE_CHECK(user_context, get_allocated_system_memory() >= (1 * sizeof(int)));
        HALIDE_CHECK(user_context, ((TestStruct *)s1)->i8 == int8_t(0));
        HALIDE_CHECK(user_context, ((TestStruct *)s1)->ui16 == uint16_t(0));
        HALIDE_CHECK(user_context, ((TestStruct *)s1)->f32 == float(0));

        arena.destroy(user_context);

        constexpr size_t count = 4 * 1024;
        void *pointers[count];
        for (size_t n = 0; n < count; ++n) {
            pointers[n] = arena.reserve(user_context, true);
        }

        for (size_t n = 0; n < count; ++n) {
            void *s1 = pointers[n];
            HALIDE_CHECK(user_context, s1 != nullptr);
            HALIDE_CHECK(user_context, ((TestStruct *)s1)->i8 == int8_t(0));
            HALIDE_CHECK(user_context, ((TestStruct *)s1)->ui16 == uint16_t(0));
            HALIDE_CHECK(user_context, ((TestStruct *)s1)->f32 == float(0));
        }

        arena.destroy(user_context);

        HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);
    }

    print(user_context) << "Success!\n";
    return 0;
}
