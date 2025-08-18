#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(NonVectorAlignedEmbededBufferTest, Basic) {
    char storage[5 * sizeof(int32_t)]{0};
    char *ptr = storage;
    ptr += sizeof(int32_t);
    Buffer<int32_t> foo((int32_t *)(ptr), 4);

    Func f;
    Var x;

    f(x) = foo(x);
    f.vectorize(x, 4);
    f.output_buffer().dim(0).set_min(0);
    auto result = f.realize({4});
}
