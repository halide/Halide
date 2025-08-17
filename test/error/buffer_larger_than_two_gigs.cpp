#include "Halide.h"
#include "halide_test_error.h"

#ifdef NDEBUG
#error "buffer_larger_than_two_gigs requires assertions"
#endif

using namespace Halide;

namespace {
void TestBufferLargerThanTwoGigs() {
    if (sizeof(void *) == 8) {
        Buffer<uint8_t> result(1 << 24, 1 << 24, 1 << 24);
    } else {
        Buffer<uint8_t> result(1 << 12, 1 << 12, 1 << 8);
    }
}
}  // namespace

TEST(ErrorTests, BufferLargerThanTwoGigs) {
    EXPECT_DEATH(TestBufferLargerThanTwoGigs(),
                 HasSubstr("Error: Overflow computing total size of buffer."));
}
