#include "internal/memory_arena.h"
#include "HalideRuntime.h"
#include "printer.h"
#include "runtime_internal.h"

using namespace Halide::Runtime::Internal;

class TestAllocator : public SystemMemoryAllocator {
public:
    size_t counter = 0;

    void *allocate(void *user_context, size_t bytes) override {
        ++counter;
        return halide_malloc(user_context, bytes);
    }

    void deallocate(void *user_context, void *ptr) override {
        halide_free(user_context, ptr);
        --counter;
    }
};

struct TestStruct {
    int8_t i8;
    uint16_t ui16;
    float f32;
};

int main(int argc, char **argv) {
    void *user_context = (void *)1;

    // test class interface
    {
        TestAllocator test_allocator;

        MemoryArena<int>::Config config = {32, 0};
        MemoryArena<int> arena(user_context, config, &test_allocator);
        int *p1 = arena.reserve(user_context);
        halide_abort_if_false(user_context, test_allocator.counter > 1);
        halide_abort_if_false(user_context, p1 != nullptr);

        int *p2 = arena.reserve(user_context);
        halide_abort_if_false(user_context, test_allocator.counter > 2);
        halide_abort_if_false(user_context, p2 != nullptr);

        arena.reclaim(user_context, p1);
        arena.destroy(user_context);
    }

    // test struct allocations
    {
        TestAllocator test_allocator;
        MemoryArena<TestStruct>::Config config = {32, 0};
        MemoryArena<TestStruct> arena(user_context, config, &test_allocator);
        TestStruct *s1 = arena.reserve(user_context);
        halide_abort_if_false(user_context, s1 != nullptr);
        halide_abort_if_false(user_context, test_allocator.counter > 1);

        arena.destroy(user_context);

        size_t count = 4 * 1024;
        TestStruct *pointers[count];
        for (size_t n = 0; n < count; ++n) {
            pointers[n] = arena.reserve(user_context);
        }

        for (size_t n = 0; n < count; ++n) {
            TestStruct *s1 = pointers[n];
            halide_abort_if_false(user_context, s1 != nullptr);
        }

        arena.destroy(user_context);
    }

    print(user_context) << "Success!\n";
    return 0;
}
