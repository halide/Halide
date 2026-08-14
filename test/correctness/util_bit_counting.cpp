#include "Halide.h"

#include <cstdint>
#include <cstdio>

// Note: this tests the internal C++ helpers Halide::Internal::popcount64()/
// clz64()/ctz64() directly (used by the compiler itself, e.g. in constant
// folding). This is deliberately distinct from
// test/correctness/bit_counting.cpp, which tests the front-end popcount()/
// count_leading_zeros()/count_trailing_zeros() *IR intrinsics* by generating
// and running Halide pipelines.

using namespace Halide::Internal;

namespace {

int failures = 0;

void check(int actual, int expected, const std::string &what) {
    if (actual != expected) {
        std::cout << "FAILED: " << what << ": got " << actual << ", expected " << expected << "\n";
        failures++;
    }
}

}  // namespace

int main(int argc, char **argv) {
    // popcount64: special-cased zero input (unlike clz64/ctz64, which assert
    // their input is nonzero), all-ones, and alternating-bit patterns.
    check(popcount64(0), 0, "popcount64(0)");
    check(popcount64(UINT64_MAX), 64, "popcount64(UINT64_MAX)");
    check(popcount64(0xAAAAAAAAAAAAAAAAULL), 32, "popcount64(0xAAAA...) (alternating bits)");
    check(popcount64(0x5555555555555555ULL), 32, "popcount64(0x5555...) (alternating bits)");

    check(clz64(1), 63, "clz64(1)");
    check(clz64(UINT64_MAX), 0, "clz64(UINT64_MAX)");
    check(ctz64(1), 0, "ctz64(1)");
    check(ctz64(UINT64_MAX), 0, "ctz64(UINT64_MAX)");
    check(ctz64(0x8000000000000000ULL), 63, "ctz64(1 << 63)");

    // A single set bit at a range of positions, including ones that
    // straddle the 32-bit half (exercising, e.g., MSVC's 32+32-bit
    // fallback path's boundary, portably, since the logic mirrors it).
    for (int k : {0, 1, 31, 32, 63}) {
        uint64_t x = uint64_t(1) << k;
        std::string what = "(k=" + std::to_string(k) + ")";
        check(popcount64(x), 1, "popcount64(1 << k) " + what);
        check(clz64(x), 63 - k, "clz64(1 << k) " + what);
        check(ctz64(x), k, "ctz64(1 << k) " + what);
    }

    // clz64(0) and ctz64(0) are deliberately not tested here: both assert
    // their input is nonzero (internal_assert), which is an internal
    // invariant violation, not a user-facing error -- not something this
    // test suite's conventions expect to be triggered.

    if (failures > 0) {
        std::cout << failures << " check(s) failed.\n";
        return 1;
    }

    printf("Success!\n");
    return 0;
}
