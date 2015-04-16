#include <stdio.h>
#include "Halide.h"

#define CHECK(f, s32, s64) \
    static_assert(offsetof(buffer_t, f) == (sizeof(void*) == 8 ? (s64) : (s32)), #f " is wrong")


int main(int argc, char **argv) {

    CHECK(dev, 0, 0);
    CHECK(host, 8, 8);
    CHECK(extent, 12, 16);
    CHECK(stride, 28, 32);
    CHECK(min, 44, 48);
    CHECK(elem_size, 60, 64);
    CHECK(host_dirty, 64, 68);
    CHECK(dev_dirty, 65, 69);
    CHECK(_padding, 66, 70);

    static_assert(sizeof(buffer_t) == (sizeof(void*) == 8 ? 72 : 68), "size is wrong");

    // Ensure alignment is at least that of a pointer.
    static_assert(alignof(buffer_t) >= alignof(uint8_t*), "align is wrong");

    printf("Success!\n");
    return 0;
}
