#include "internal/linked_list.h"
#include "HalideRuntime.h"
#include "printer.h"
#include "runtime_internal.h"

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
        LinkedList<int> list(user_context, 64);
        halide_abort_if_false(user_context, list.size() == 0);

        list.append(user_context, 12);  // contents: 12
        halide_abort_if_false(user_context, list.size() == 1);
        halide_abort_if_false(user_context, (list.front() != nullptr));
        halide_abort_if_false(user_context, (list.back() != nullptr));
        halide_abort_if_false(user_context, list.front()->value == 12);
        halide_abort_if_false(user_context, list.back()->value == 12);

        list.append(user_context, 34);  // contents: 12, 34
        halide_abort_if_false(user_context, list.size() == 2);
        halide_abort_if_false(user_context, list.back()->value == 34);

        list.insert_before(user_context, list.back(), 56);  // contents: 12, 56, 34
        halide_abort_if_false(user_context, list.size() == 3);
        halide_abort_if_false(user_context, list.back()->value == 34);

        list.prepend(user_context, 78);  // contents: 78, 12, 56, 34
        halide_abort_if_false(user_context, list.size() == 4);
        halide_abort_if_false(user_context, list.front()->value == 78);
        halide_abort_if_false(user_context, list.back()->value == 34);

        list.pop_front(user_context);  // contents: 12, 56, 34
        halide_abort_if_false(user_context, list.size() == 3);
        halide_abort_if_false(user_context, list.front()->value == 12);
        halide_abort_if_false(user_context, list.back()->value == 34);

        list.pop_back(user_context);  // contents: 12, 56
        halide_abort_if_false(user_context, list.size() == 2);
        halide_abort_if_false(user_context, list.front()->value == 12);
        halide_abort_if_false(user_context, list.back()->value == 56);

        list.clear(user_context);
        halide_abort_if_false(user_context, list.size() == 0);
    }

    // test struct storage
    {
        LinkedList<TestStruct> list;
        halide_abort_if_false(user_context, list.size() == 0);

        TestStruct s1 = {8, 16, 32.0f};
        list.append(user_context, s1);
        halide_abort_if_false(user_context, list.size() == 1);

        const TestStruct &e1 = list.front()->value;
        halide_abort_if_false(user_context, e1.i8 == s1.i8);
        halide_abort_if_false(user_context, e1.ui16 == s1.ui16);
        halide_abort_if_false(user_context, e1.f32 == s1.f32);

        TestStruct s2 = {1, 2, 3.0f};
        list.prepend(user_context, s2);
        halide_abort_if_false(user_context, list.size() == 2);

        const TestStruct &e2 = list.front()->value;
        halide_abort_if_false(user_context, e2.i8 == s2.i8);
        halide_abort_if_false(user_context, e2.ui16 == s2.ui16);
        halide_abort_if_false(user_context, e2.f32 == s2.f32);
    }

    print(user_context) << "Success!\n";
    return 0;
}
