#include <stdio.h>
#include "Halide.h"

#define CHECK(f, s32, s64) \
    static_assert(offsetof(halide_buffer_t, f) == (sizeof(void*) == 8 ? (s64) : (s32)), #f " is wrong")

#ifdef _MSC_VER
    // VC2013 doesn't support alignof, apparently
    #define ALIGN_OF(x) __alignof(x)
#else
    #define ALIGN_OF(x) alignof(x)
#endif

int main(int argc, char **argv) {

    CHECK(device, 0, 0);
    CHECK(device_interface, 8, 8);
    CHECK(host, 12, 16);
    CHECK(flags, 16, 24);
    CHECK(type, 24, 32);
    CHECK(dimensions, 28, 36);
    CHECK(dim, 32, 40);
    CHECK(padding, 36, 48);

    static_assert((sizeof(void *) == 8 && sizeof(halide_buffer_t) == 56) ||
                  (sizeof(void *) == 4 && sizeof(halide_buffer_t) == 40),
                  "size is wrong");

    static_assert(sizeof(halide_dimension_t) == 16, "size of halide_dimension_t is wrong");

    // Ensure alignment is at least that of a pointer.
    static_assert(ALIGN_OF(halide_buffer_t) >= ALIGN_OF(uint8_t*), "align is wrong");

    printf("Success!\n");
    return 0;
}
