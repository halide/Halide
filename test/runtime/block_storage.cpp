#include "HalideRuntime.h"

#include "common.h"
#include "printer.h"

#include "internal/block_storage.h"

using namespace Halide::Runtime::Internal;

struct TestStruct {
    int8_t i8;
    uint16_t ui16;
    float f32;
};

template<typename T>
T read_as(const BlockStorage &bs, size_t index) {
    const T *ptr = static_cast<const T *>(bs[index]);
    return *ptr;
}

int main(int argc, char **argv) {
    void *user_context = (void *)1;

    // test class interface
    {
        BlockStorage::Config config = BlockStorage::default_config();
        config.entry_size = sizeof(int);

        BlockStorage bs(user_context, config);
        bs.reserve(user_context, 256);
        HALIDE_CHECK(user_context, bs.size() == 0);

        int a1[4] = {12, 34, 56, 78};
        bs.append(user_context, &a1[0]);
        HALIDE_CHECK(user_context, bs.size() == 1);
        HALIDE_CHECK(user_context, read_as<int>(bs, 0) == a1[0]);

        bs.append(user_context, &a1[1]);
        HALIDE_CHECK(user_context, bs.size() == 2);
        HALIDE_CHECK(user_context, read_as<int>(bs, 1) == a1[1]);

        bs.insert(user_context, 1, &a1[2]);
        HALIDE_CHECK(user_context, bs.size() == 3);
        HALIDE_CHECK(user_context, read_as<int>(bs, 0) == a1[0]);
        HALIDE_CHECK(user_context, read_as<int>(bs, 1) == a1[2]);  // inserted here
        HALIDE_CHECK(user_context, read_as<int>(bs, 2) == a1[1]);

        bs.prepend(user_context, &a1[3]);
        HALIDE_CHECK(user_context, bs.size() == 4);
        HALIDE_CHECK(user_context, read_as<int>(bs, 0) == a1[3]);

        int a2[] = {98, 76, 54, 32, 10};
        size_t a2_size = 5;
        bs.fill(user_context, a2, a2_size);
        HALIDE_CHECK(user_context, bs.size() == a2_size);
        HALIDE_CHECK(user_context, read_as<int>(bs, 0) == a2[0]);
        HALIDE_CHECK(user_context, read_as<int>(bs, 1) == a2[1]);
        HALIDE_CHECK(user_context, read_as<int>(bs, 2) == a2[2]);
        HALIDE_CHECK(user_context, read_as<int>(bs, 3) == a2[3]);
        HALIDE_CHECK(user_context, read_as<int>(bs, 4) == a2[4]);

        int a3[] = {77, 66, 55};
        size_t a3_size = 3;
        bs.insert(user_context, 2, a3, a3_size);
        HALIDE_CHECK(user_context, bs.size() == (a2_size + a3_size));
        HALIDE_CHECK(user_context, read_as<int>(bs, 0) == a2[0]);
        HALIDE_CHECK(user_context, read_as<int>(bs, 1) == a2[1]);
        HALIDE_CHECK(user_context, read_as<int>(bs, 2) == a3[0]);  // a3 inserted here
        HALIDE_CHECK(user_context, read_as<int>(bs, 3) == a3[1]);
        HALIDE_CHECK(user_context, read_as<int>(bs, 4) == a3[2]);
        HALIDE_CHECK(user_context, read_as<int>(bs, 5) == a2[2]);  // a2 resumes here
        HALIDE_CHECK(user_context, read_as<int>(bs, 6) == a2[3]);
        HALIDE_CHECK(user_context, read_as<int>(bs, 7) == a2[4]);

        bs.pop_front(user_context);
        bs.pop_front(user_context);

        bs.pop_back(user_context);
        bs.pop_back(user_context);

        HALIDE_CHECK(user_context, bs.size() == (a2_size + a3_size - 4));
        HALIDE_CHECK(user_context, read_as<int>(bs, 0) == a3[0]);
        HALIDE_CHECK(user_context, read_as<int>(bs, 1) == a3[1]);
        HALIDE_CHECK(user_context, read_as<int>(bs, 2) == a3[2]);
        HALIDE_CHECK(user_context, read_as<int>(bs, 3) == a2[2]);

        bs.clear(user_context);
        HALIDE_CHECK(user_context, bs.size() == 0);
    }

    // test copy and equality
    {
        BlockStorage::Config config = BlockStorage::default_config();
        config.entry_size = sizeof(int);

        int a1[] = {98, 76, 54, 32, 10};
        size_t a1_size = 5;

        int a2[] = {77, 66, 55};
        size_t a2_size = 3;

        BlockStorage bs1(user_context, config);
        bs1.fill(user_context, a1, a1_size);

        BlockStorage bs2(user_context, config);
        bs2.fill(user_context, a2, a2_size);

        BlockStorage bs3(bs1);

        HALIDE_CHECK(user_context, bs1.size() == (a1_size));
        HALIDE_CHECK(user_context, bs2.size() == (a2_size));
        HALIDE_CHECK(user_context, bs3.size() == bs1.size());

        HALIDE_CHECK(user_context, bs1 != bs2);
        HALIDE_CHECK(user_context, bs1 == bs3);

        bs2 = bs1;
        HALIDE_CHECK(user_context, bs1 == bs2);
    }

    // test struct storage
    {
        BlockStorage::Config config = BlockStorage::default_config();
        config.entry_size = sizeof(TestStruct);

        BlockStorage bs(user_context, config);
        HALIDE_CHECK(user_context, bs.size() == 0);

        TestStruct s1 = {8, 16, 32.0f};
        bs.append(user_context, &s1);
        HALIDE_CHECK(user_context, bs.size() == 1);

        const TestStruct e1 = read_as<TestStruct>(bs, 0);
        HALIDE_CHECK(user_context, e1.i8 == s1.i8);
        HALIDE_CHECK(user_context, e1.ui16 == s1.ui16);
        HALIDE_CHECK(user_context, e1.f32 == s1.f32);

        TestStruct s2 = {1, 2, 3.0f};
        bs.prepend(user_context, &s2);
        HALIDE_CHECK(user_context, bs.size() == 2);

        const TestStruct e2 = read_as<TestStruct>(bs, 0);
        HALIDE_CHECK(user_context, e2.i8 == s2.i8);
        HALIDE_CHECK(user_context, e2.ui16 == s2.ui16);
        HALIDE_CHECK(user_context, e2.f32 == s2.f32);
    }

    print(user_context) << "Success!\n";
    return 0;
}
