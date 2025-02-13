#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {

    Param<int64_t> i64("i64");
    Param<int32_t> i32("i32");
    Param<uint64_t> u64("u64");
    Param<uint32_t> u32("u32");
    Param<uint16_t> u16("u16");

    // A list of Exprs we should be able to prove true by analyzing the bitwise op we do
    Expr exprs[] = {
        // Manipulate or isolate the low bits
        (i64 & 1) < 2,
        (i64 & 1) >= 0,
        (i64 | 1) % 2 == 1,
        (i64 & 2) <= 2,
        (i64 & 2) >= 0,
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
        ((i32 & cast<int32_t>(u16 << 3)) | 5) % 8 == 5,
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
    };

    for (auto e : exprs) {
        if (!can_prove(e)) {
            std::cerr << "Failed to prove: " << e << "\n";
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}
