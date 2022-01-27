#include "HalideRuntime.h"
#include "runtime_internal.h"
#include "internal/memory_arena.h"
#include "printer.h"

using namespace Halide::Runtime::Internal;

static size_t counter = 0;

static void construct_int(void*, int* ptr) {
    *ptr = 1;
    ++counter;
}

static void destruct_int(void*, int* ptr) {
    *ptr = 2;
    --counter;
}

struct TestStruct {
    int8_t i8;
    uint16_t ui16;
    float f32;
};

static void construct_struct(void*, TestStruct* ptr) {
    ptr->i8 = 8;
    ptr->ui16 = 16;
    ptr->f32 = 32.0f;
    ++counter;
}

static void destruct_struct(void*, TestStruct* ptr) {
    ptr->i8 = 0;
    ptr->ui16 = 0;
    ptr->f32 = 0.0f;
    --counter;
}

int main(int argc, char **argv) {
    void* user_context = (void*)1;

    // test class interface
    {
        MemoryArena<int>::AllocatorFns alloc = MemoryArena<int>::default_allocator();
        MemoryArena<int>::InitializerFns initializer = { construct_int, destruct_int };
        MemoryArena<int> arena(user_context, 32, alloc, initializer);
        int* p1 = arena.reserve(user_context);
        halide_abort_if_false(user_context, counter == 1);
        halide_abort_if_false(user_context, p1 != nullptr);
        halide_abort_if_false(user_context, *p1 == 1);

        int* p2 = arena.reserve(user_context);
        halide_abort_if_false(user_context, counter == 2);
        halide_abort_if_false(user_context, p2 != nullptr);
        halide_abort_if_false(user_context, *p2 == 1);

        arena.reclaim(user_context, p1);
        halide_abort_if_false(user_context, counter == 1);

        arena.destroy(user_context);
        halide_abort_if_false(user_context, counter == 0);
    }

    // test struct allocations
    {
        MemoryArena<TestStruct>::AllocatorFns alloc = MemoryArena<TestStruct>::default_allocator();
        MemoryArena<TestStruct>::InitializerFns initializer = { construct_struct, destruct_struct };
        MemoryArena<TestStruct> arena(user_context, 32, alloc, initializer);
        TestStruct* s1 = arena.reserve(user_context);
        halide_abort_if_false(user_context, counter == 1);
        halide_abort_if_false(user_context, s1->i8 == 8);
        halide_abort_if_false(user_context, s1->ui16 == 16);
        halide_abort_if_false(user_context, s1->f32 == 32.0f);

        arena.destroy(user_context);
        halide_abort_if_false(user_context, counter == 0);

        size_t count = 4 * 1024;
        TestStruct* pointers[count];
        for(size_t n = 0; n < count; ++n) {
            pointers[n] = arena.reserve(user_context);
        }
        halide_abort_if_false(user_context, counter == count);

        for(size_t n = 0; n < count; ++n) {
            TestStruct* s1 = pointers[n];
            halide_abort_if_false(user_context, s1->i8 == 8);
            halide_abort_if_false(user_context, s1->ui16 == 16);
            halide_abort_if_false(user_context, s1->f32 == 32.0f);
        }

        arena.destroy(user_context);
        halide_abort_if_false(user_context, counter == 0);
    }

    print(user_context) << "Success!\n";
    return 0;
}
