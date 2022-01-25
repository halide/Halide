#include "HalideRuntime.h"
#include "runtime_internal.h"
#include "printer.h"
#include "internal/block_storage.h"

using namespace Halide::Runtime::Internal;

struct TestStruct {
    int8_t i8;
    uint16_t ui16;
    float f32;
};

int main(int argc, char **argv) {
    void* user_context = (void*)1;

    // test class interface
    {
        BlockStorage<int> bs(user_context, 256);
        halide_abort_if_false(user_context, bs.size() == 0);

        bs.append(user_context, 12);
        halide_abort_if_false(user_context, bs.size() == 1);
        halide_abort_if_false(user_context, bs[0] == 12);

        bs.append(user_context, 34);
        halide_abort_if_false(user_context, bs.size() == 2);
        halide_abort_if_false(user_context, bs[1] == 34);

        bs.insert(user_context, 1, 56);
        halide_abort_if_false(user_context, bs.size() == 3);
        halide_abort_if_false(user_context, bs[0] == 12);
        halide_abort_if_false(user_context, bs[1] == 56);
        halide_abort_if_false(user_context, bs[2] == 34);

        bs.prepend(user_context, 78);
        halide_abort_if_false(user_context, bs.size() == 4);
        halide_abort_if_false(user_context, bs[0] == 78);

        int a1[] = { 98, 76, 54, 32, 10 };
        size_t a1_size = 5;
        bs.assign(user_context, a1, a1_size);
        halide_abort_if_false(user_context, bs.size() == a1_size);
        halide_abort_if_false(user_context, bs[0] == a1[0]);
        halide_abort_if_false(user_context, bs[1] == a1[1]);
        halide_abort_if_false(user_context, bs[2] == a1[2]);
        halide_abort_if_false(user_context, bs[3] == a1[3]);
        halide_abort_if_false(user_context, bs[4] == a1[4]);

        int a2[] = { 77, 66, 55 };
        size_t a2_size = 3;
        bs.insert(user_context, 2, a2, a2_size);
        halide_abort_if_false(user_context, bs.size() == (a1_size + a2_size));
        halide_abort_if_false(user_context, bs[0] == a1[0]);
        halide_abort_if_false(user_context, bs[1] == a1[1]);
        halide_abort_if_false(user_context, bs[2] == a2[0]);
        halide_abort_if_false(user_context, bs[3] == a2[1]);
        halide_abort_if_false(user_context, bs[4] == a2[2]);
        halide_abort_if_false(user_context, bs[5] == a1[2]);
        halide_abort_if_false(user_context, bs[6] == a1[3]);
        halide_abort_if_false(user_context, bs[7] == a1[4]);

        bs.pop_front(user_context);
        bs.pop_front(user_context);

        bs.pop_back(user_context);
        bs.pop_back(user_context);

        halide_abort_if_false(user_context, bs.size() == (a1_size + a2_size - 4));
        halide_abort_if_false(user_context, bs[0] == a2[0]);
        halide_abort_if_false(user_context, bs[1] == a2[1]);
        halide_abort_if_false(user_context, bs[2] == a2[2]);
        halide_abort_if_false(user_context, bs[3] == a1[2]);

        bs.clear(user_context);
        halide_abort_if_false(user_context, bs.size() == 0);
    }

    // test copy and equality
    {
        int a1[] = { 98, 76, 54, 32, 10 };
        size_t a1_size = 5;

        int a2[] = { 77, 66, 55 };
        size_t a2_size = 3;

        BlockStorage<int> bs1(user_context, a1, a1_size);
        BlockStorage<int> bs2(user_context, a2, a2_size);
        BlockStorage<int> bs3(bs1);

        halide_abort_if_false(user_context, bs1.size() == (a1_size));
        halide_abort_if_false(user_context, bs2.size() == (a2_size));
        halide_abort_if_false(user_context, bs3.size() == bs1.size());

        halide_abort_if_false(user_context, bs1 != bs2);
        halide_abort_if_false(user_context, bs1 == bs3);

        bs2 = bs1;
        halide_abort_if_false(user_context, bs1 == bs2);
    }

    // test struct storage
    {
        BlockStorage<TestStruct> bs;
        halide_abort_if_false(user_context, bs.size() == 0);

        TestStruct s1 = {8, 16, 32.0f};
        bs.append(user_context, s1);
        halide_abort_if_false(user_context, bs.size() == 1);

        const TestStruct& e1 = bs[0];
        halide_abort_if_false(user_context, e1.i8 == s1.i8);
        halide_abort_if_false(user_context, e1.ui16 == s1.ui16);
        halide_abort_if_false(user_context, e1.f32 == s1.f32);

        TestStruct s2 = {1, 2, 3.0f};
        bs.prepend(user_context, s2);
        halide_abort_if_false(user_context, bs.size() == 2);

        const TestStruct& e2 = bs[0];
        halide_abort_if_false(user_context, e2.i8 == s2.i8);
        halide_abort_if_false(user_context, e2.ui16 == s2.ui16);
        halide_abort_if_false(user_context, e2.f32 == s2.f32);
    }

    print(user_context) << "Success!\n";
    return 0;
}
