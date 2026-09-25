#include "Halide.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>

#ifdef NDEBUG
#error "buffer_larger_than_two_gigs requires assertions"
#endif

// This test intentionally trips a raw C assert() inside Buffer's overflow
// check (rather than throwing a Halide::Error), so unlike the other
// converted error tests it cannot be caught with expect_user_error()/
// error_test(). assert() calls abort(), which CTest's WILL_FAIL cannot
// invert (system-level crashes always fail regardless of WILL_FAIL), so we
// install our own SIGABRT handler that reports success and exits cleanly.
using namespace Halide;

extern "C" void expect_abort(int) {
    printf("Success!\n");
    fflush(stdout);
    std::_Exit(0);
}

int main(int argc, char **argv) {
    signal(SIGABRT, expect_abort);

    if (sizeof(void *) == 8) {
        Buffer<uint8_t> result(1 << 24, 1 << 24, 1 << 24);
    } else {
        Buffer<uint8_t> result(1 << 12, 1 << 12, 1 << 8);
    }

    printf("This line should be unreachable!\n");
    return 1;
}
