#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
Func add2(Func in) {
    Func a("ext");
    a(_) = in(_) + 2;
    return a;
}
}

TEST(UniqueFuncImageTest, UniqueFuncImage) {
    Func ext1("ext"), ext2("ext");
    Var x("x");

    ext1(x) = x + 1;
    ext2(x) = x + 2;

    EXPECT_NE(ext1.name(), ext2.name()) << "Programmer-specified function names have not been made unique!";

    Buffer<int> out1, out2;
    ASSERT_NO_THROW(out1 = ext1.realize({10}));
    ASSERT_NO_THROW(out2 = ext2.realize({10}));

    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(out1(i), i + 1) << "Incorrect result from call to ext1 at index " << i;
    }

    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(out2(i), i + 2) << "Incorrect result from call to ext2 at index " << i;
    }

    Func out1_as_func(out1);
    Func ext3 = add2(out1_as_func);

    Buffer<int> out3;
    ASSERT_NO_THROW(out3 = ext3.realize({10}));

    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(out3(i), i + 3) << "Incorrect result from call to add2 at index " << i;
    }
}
