#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {

    Param<int64_t> i64("i64");
    Param<int32_t> i32("i32");
    Param<int32_t> i16("i16");
    Param<uint64_t> u64("u64");
    Param<uint32_t> u32("u32");
    Param<uint16_t> u16("u16");
    Param<uint8_t> u8("u8");

    // A list of Exprs we should be able to prove true by analyzing the bitwise op we do
    Expr exprs[] = {
        // Manipulate or isolate the low bits
        (i64 & 1) < 2,
        (i64 & 1) >= 0,
        (i64 | 1) % 2 == 1,
        (i64 & 2) <= 2,
        (i64 & 2) >= 0,

        (min(i32, -1) ^ (i32 & 255)) < 0,

        // The next is currently beyond us, because we'd have to carry expr
        // information in the bits_known format through the modulus
        // op. Currently just known the second-lowest-bit is 2 but nothing else
        // doesn't give us an alignment or bounds.
        // (i64 | 2) % 4 >= 2,

        (u64 & 1) < 2,
        (u64 & 1) >= 0,
        (u64 | 1) % 2 == 1,
        (u64 & 2) <= 2,
        (u64 & 2) >= 0,
        // Beyond us for the same reason as above
        // (u64 | 2) % 4 >= 2,

        // Manipulate or isolate the high bits, in various types, starting with
        // two common idioms for aligning a value to a multiple of 16.
        ((i32 & ~15) & 15) == 0,
        ((i32 & ~15) % 16) == 0,
        ((i32 & cast<int32_t>(u16 << 2)) | 5) % 8 == 5,
        (i32 | 0x80000000) < 0,
        cast<int32_t>(u32 & ~0x80000000) >= 0,
        (cast<uint32_t>(u16) & (cast<uint32_t>(u16) << 16)) == 0,

        // Setting or unsetting bits makes a number larger or smaller, respectively
        (i32 & cast<int32_t>(u16)) >= 0,
        (i32 & cast<int32_t>(u16)) < 0x10000,

        // What happens when the known bits say a uint is too big to represent
        // in our bounds? Not currently reachable, because the (intentional)
        // overflow on the cast to uint causes ConstantInterval to just drop all
        // information.
        // (cast<uint64_t>(i64 | -2)) > u32

        // Flipping the bits of an int flips it without overflow. I.e. for a
        // uint8, ~x is 255 - x. This gives us bounds information.
        (~clamp(u8, 3, 5)) >= 255 - 5,
        (~clamp(u8, 3, 5)) <= 255 - 3,

        // If we knew the trailing bits before, we still know them after
        (~(i32 * 16)) % 16 == 15,

    };

    // Check we're not inferring *too* much, with variants of the above that
    // shouldn't be provable one way or the other.
    Expr negative_exprs[] = {
        (i64 & 3) < 2,
        (i64 & 3) >= 1,
        (i64 | 1) % 4 == 1,
        (i64 & 3) <= 2,
        (i64 & 3) >= 1,

        (max(u32, 1000) ^ (u64 & 255)) >= 1000,

        (u64 & 3) < 2,
        (u64 & 3) >= 1,
        (u64 | 1) % 4 == 1,
        (u64 & 3) <= 2,
        (u64 & 2) >= 1,

        ((i32 & ~15) & 31) == 0,
        ((i32 & ~15) % 32) == 0,
        ((i32 & cast<int32_t>(u16 << 1)) | 5) % 8 == 5,
        (i32 | 0x80000000) < -1,
        cast<int16_t>(u32 & ~0x80000000) >= 0,
        (cast<uint32_t>(u16) & (cast<uint32_t>(u16) << 15)) == 0,

        (i32 & cast<int32_t>(u16)) >= 1,
        (i32 & cast<int32_t>(u16)) < 0xffff,

        (~clamp(u8, 3, 5)) >= 255 - 4,
    };

    for (auto e : exprs) {
        if (!can_prove(e)) {
            std::cerr << "Failed to prove: " << e << "\n";
            return -1;
        }
    }

    for (auto e : negative_exprs) {
        if (is_const(simplify(e))) {
            std::cerr << "Should not have been able to prove or disprove: " << e << "\n";
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
