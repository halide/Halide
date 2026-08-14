#include "Halide.h"

#include <cstdio>

using namespace Halide::Internal;

namespace {

int failures = 0;

void check(bool actual, bool expected, const std::string &what) {
    if (actual != expected) {
        std::cout << "FAILED: " << what << ": got " << actual << ", expected " << expected << "\n";
        failures++;
    }
}

void check(int64_t actual, int64_t expected, const std::string &what) {
    if (actual != expected) {
        std::cout << "FAILED: " << what << ": got " << actual << ", expected " << expected << "\n";
        failures++;
    }
}

// Independently derived (via left-shift rather than the implementation's
// right-shift-of-all-ones) so this doesn't just mirror the same formula
// (and thus the same potential bug) being tested.
int64_t max_val_for_bits(int bits) {
    if (bits == 64) {
        return INT64_MAX;
    }
    return (int64_t(1) << (bits - 1)) - 1;
}

int64_t min_val_for_bits(int bits) {
    return -max_val_for_bits(bits) - 1;
}

void check_bits(int bits) {
    const std::string b = "(bits=" + std::to_string(bits) + ") ";
    const int64_t max_val = max_val_for_bits(bits);
    const int64_t min_val = min_val_for_bits(bits);
    int64_t result = -1;

    // add: identities and boundary overflow in both directions.
    check(add_would_overflow(bits, max_val, 0), false, b + "add_would_overflow(max, 0)");
    check(add_with_overflow(bits, max_val, 0, &result), true, b + "add_with_overflow(max, 0)");
    check(result, max_val, b + "add_with_overflow(max, 0) result");

    check(add_would_overflow(bits, max_val, 1), true, b + "add_would_overflow(max, 1)");
    check(add_with_overflow(bits, max_val, 1, &result), false, b + "add_with_overflow(max, 1)");
    check(result, (int64_t)0, b + "add_with_overflow(max, 1) result zeroed on overflow");

    check(add_would_overflow(bits, min_val, -1), true, b + "add_would_overflow(min, -1)");
    check(add_with_overflow(bits, min_val, -1, &result), false, b + "add_with_overflow(min, -1)");
    check(result, (int64_t)0, b + "add_with_overflow(min, -1) result zeroed on overflow");

    // sub: identities and boundary overflow in both directions.
    check(sub_would_overflow(bits, min_val, 0), false, b + "sub_would_overflow(min, 0)");
    check(sub_with_overflow(bits, min_val, 0, &result), true, b + "sub_with_overflow(min, 0)");
    check(result, min_val, b + "sub_with_overflow(min, 0) result");

    check(sub_would_overflow(bits, min_val, 1), true, b + "sub_would_overflow(min, 1)");
    check(sub_with_overflow(bits, min_val, 1, &result), false, b + "sub_with_overflow(min, 1)");
    check(result, (int64_t)0, b + "sub_with_overflow(min, 1) result zeroed on overflow");

    check(sub_would_overflow(bits, max_val, -1), true, b + "sub_would_overflow(max, -1)");
    check(sub_with_overflow(bits, max_val, -1, &result), false, b + "sub_with_overflow(max, -1)");
    check(result, (int64_t)0, b + "sub_with_overflow(max, -1) result zeroed on overflow");

    // mul: zero identity, one identity, boundary overflow, and the a==-1
    // special case (min_val has no positive counterpart to negate into).
    check(mul_would_overflow(bits, 0, min_val), false, b + "mul_would_overflow(0, min)");
    check(mul_with_overflow(bits, 0, min_val, &result), true, b + "mul_with_overflow(0, min)");
    check(result, (int64_t)0, b + "mul_with_overflow(0, min) result");

    check(mul_would_overflow(bits, max_val, 1), false, b + "mul_would_overflow(max, 1)");
    check(mul_with_overflow(bits, max_val, 1, &result), true, b + "mul_with_overflow(max, 1)");
    check(result, max_val, b + "mul_with_overflow(max, 1) result");

    check(mul_would_overflow(bits, max_val, 2), true, b + "mul_would_overflow(max, 2)");
    check(mul_with_overflow(bits, max_val, 2, &result), false, b + "mul_with_overflow(max, 2)");
    check(result, (int64_t)0, b + "mul_with_overflow(max, 2) result zeroed on overflow");

    check(mul_would_overflow(bits, -1, min_val), true, b + "mul_would_overflow(-1, min) (a==-1 special case)");
    check(mul_with_overflow(bits, -1, min_val, &result), false, b + "mul_with_overflow(-1, min)");
    check(result, (int64_t)0, b + "mul_with_overflow(-1, min) result zeroed on overflow");

    check(mul_would_overflow(bits, -1, min_val + 1), false, b + "mul_would_overflow(-1, min+1) (just inside range)");
    check(mul_with_overflow(bits, -1, min_val + 1, &result), true, b + "mul_with_overflow(-1, min+1)");
    check(result, -(min_val + 1), b + "mul_with_overflow(-1, min+1) result");
}

}  // namespace

int main(int argc, char **argv) {
    for (int bits : {8, 16, 32, 64}) {
        check_bits(bits);
    }

    if (failures > 0) {
        std::cout << failures << " check(s) failed.\n";
        return 1;
    }

    printf("Success!\n");
    return 0;
}
