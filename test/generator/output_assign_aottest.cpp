#include <stdio.h>

#include "HalideBuffer.h"
#include "output_assign.h"

using namespace Halide::Runtime;

constexpr int kEdge = 32;

Buffer<int32_t, 2> expected(int extra) {
    Buffer<int32_t, 2> b(kEdge, kEdge);
    b.for_each_element([&b, extra](int x, int y) {
        b(x, y) = (int32_t)(x + y + extra);
    });
    return b;
}

void compare(Buffer<int32_t, 2> expected, Buffer<int32_t, 2> actual) {
    expected.for_each_element([expected, actual](int x, int y) {
        if (expected(x, y) != actual(x, y)) {
            printf("expected(%d, %d) = %d, actual(%d, %d) = %d\n",
                   x, y, expected(x, y), x, y, actual(x, y));
            exit(1);
        }
    });
}

int main(int argc, char **argv) {
    Buffer<int32_t, 2> actual0(kEdge, kEdge);
    Buffer<int32_t, 2> actual1(kEdge, kEdge);
    Buffer<int32_t, 2> actual2(kEdge, kEdge);
    output_assign(actual0, actual1, actual2);

    compare(expected(0), actual0);
    compare(expected(1), actual1);
    compare(expected(2), actual2);

    printf("Success!\n");
    return 0;
}
