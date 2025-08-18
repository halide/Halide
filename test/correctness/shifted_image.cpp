#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(ShiftedImageTest, Basic) {
    halide_dimension_t shape[] = {{100, 10, 1},
                                  {300, 10, 10},
                                  {500, 10, 100},
                                  {400, 10, 1000}};
    Buffer<int> buf(nullptr, 4, shape);
    buf.allocate();

    buf.data()[0] = 17;
    EXPECT_EQ(buf(100, 300, 500, 400), 17) << "Image indexing into buffers with non-zero mins is broken";
}
