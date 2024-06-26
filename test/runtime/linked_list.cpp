#include "HalideRuntime.h"

#include "common.h"
#include "printer.h"

#include "internal/linked_list.h"

using namespace Halide::Runtime::Internal;

struct TestStruct {
    int8_t i8;
    uint16_t ui16;
    float f32;
};

template<typename T>
T read_as(const LinkedList::EntryType *entry_ptr) {
    const T *ptr = static_cast<const T *>(entry_ptr->value);
    return *ptr;
}

int main(int argc, char **argv) {
    void *user_context = (void *)1;
    SystemMemoryAllocatorFns test_allocator = {allocate_system, deallocate_system};

    // test class interface
    {
        LinkedList list(user_context, sizeof(int), 64, test_allocator);
        HALIDE_CHECK(user_context, list.size() == 0);

        const int i0 = 12;
        list.append(user_context, &i0);  // contents: 12
        HALIDE_CHECK(user_context, list.size() == 1);
        HALIDE_CHECK(user_context, (list.front() != nullptr));
        HALIDE_CHECK(user_context, (list.back() != nullptr));
        HALIDE_CHECK(user_context, read_as<int>(list.front()) == i0);
        HALIDE_CHECK(user_context, read_as<int>(list.back()) == i0);

        const int i1 = 34;
        list.append(user_context, &i1);  // contents: 12, 34
        HALIDE_CHECK(user_context, list.size() == 2);
        HALIDE_CHECK(user_context, read_as<int>(list.back()) == i1);

        const int i2 = 56;
        list.insert_before(user_context, list.back(), &i2);  // contents: 12, 56, 34
        HALIDE_CHECK(user_context, list.size() == 3);
        HALIDE_CHECK(user_context, read_as<int>(list.back()) == i1);

        const int i3 = 78;
        list.prepend(user_context, &i3);  // contents: 78, 12, 56, 34
        HALIDE_CHECK(user_context, list.size() == 4);
        HALIDE_CHECK(user_context, read_as<int>(list.front()) == i3);
        HALIDE_CHECK(user_context, read_as<int>(list.back()) == i1);

        list.pop_front(user_context);  // contents: 12, 56, 34
        HALIDE_CHECK(user_context, list.size() == 3);
        HALIDE_CHECK(user_context, read_as<int>(list.front()) == i0);
        HALIDE_CHECK(user_context, read_as<int>(list.back()) == i1);

        list.pop_back(user_context);  // contents: 12, 56
        HALIDE_CHECK(user_context, list.size() == 2);
        HALIDE_CHECK(user_context, read_as<int>(list.front()) == i0);
        HALIDE_CHECK(user_context, read_as<int>(list.back()) == i2);

        list.clear(user_context);
        HALIDE_CHECK(user_context, list.size() == 0);

        size_t count = 4 * 1024;
        for (size_t n = 0; n < count; ++n) {
            list.append(user_context, &n);
        }
        HALIDE_CHECK(user_context, list.size() == count);

        list.destroy(user_context);
        HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);
    }

    // test struct storage
    {
        LinkedList list(user_context, sizeof(TestStruct), 32, test_allocator);
        HALIDE_CHECK(user_context, list.size() == 0);

        TestStruct s1 = {8, 16, 32.0f};
        list.append(user_context, &s1);
        HALIDE_CHECK(user_context, list.size() == 1);

        const TestStruct e1 = read_as<TestStruct>(list.front());
        HALIDE_CHECK(user_context, e1.i8 == s1.i8);
        HALIDE_CHECK(user_context, e1.ui16 == s1.ui16);
        HALIDE_CHECK(user_context, e1.f32 == s1.f32);

        TestStruct s2 = {1, 2, 3.0f};
        list.prepend(user_context, &s2);
        HALIDE_CHECK(user_context, list.size() == 2);

        TestStruct e2 = read_as<TestStruct>(list.front());
        HALIDE_CHECK(user_context, e2.i8 == s2.i8);
        HALIDE_CHECK(user_context, e2.ui16 == s2.ui16);
        HALIDE_CHECK(user_context, e2.f32 == s2.f32);

        list.destroy(user_context);
        HALIDE_CHECK(user_context, get_allocated_system_memory() == 0);
    }

    print(user_context) << "Success!\n";
    return 0;
}
