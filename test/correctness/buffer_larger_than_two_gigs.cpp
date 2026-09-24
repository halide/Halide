#include "Halide.h"
#include "expect_user_error.h"
#include <cstdio>

#ifdef NDEBUG
#error "buffer_larger_than_two_gigs requires assertions"
#endif

using namespace Halide;
int main(int argc, char **argv) {
    return error_test("buffer_larger_than_two_gigs", "Error: Overflow computing total size of buffer.", []() {
        if (sizeof(void *) == 8) {
            Buffer<uint8_t> result(1 << 24, 1 << 24, 1 << 24);
        } else {
            Buffer<uint8_t> result(1 << 12, 1 << 12, 1 << 8);
        }
    });
}
