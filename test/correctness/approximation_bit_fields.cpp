#include "Halide.h"

#include <cstdio>

using namespace Halide;

namespace {

// Two fields in 16-bit words: four 3-bit elements of "a" at bits 0, 3, 6, 9,
// and one 4-bit element of "b" at bit 12; batched over a third dimension.
int test_multiple_fields() {
    Var e("e"), r("r"), n("n");
    Func a("a_in"), b("b_in");
    a(e, r, n) = cast<uint8_t>((e * 5 + r + n) % 8);
    b(e, r, n) = cast<uint8_t>((e * 7 + r * 3 + n) % 16);
    Approximation fields = BitFields(UInt(16), {{"a", 3, 4, 0, 3}, {"b", 4, 1, 12}});
    EncodeResult enc = fields.encode({a, b});
    enc.encoded[0].compute_root();
    DecodeResult dec = fields.decode(enc.encoded);
    Buffer<uint16_t> words = enc.encoded[0].realize({2, 3, 2});
    Buffer<uint8_t> a_out = dec.decoded[0].realize({8, 3, 2});
    Buffer<uint8_t> b_out = dec.decoded[1].realize({2, 3, 2});
    for (int nn = 0; nn < 2; nn++) {
        for (int rr = 0; rr < 3; rr++) {
            for (int w = 0; w < 2; w++) {
                int expected = ((w * 7 + rr * 3 + nn) % 16) << 12;
                for (int s = 0; s < 4; s++) {
                    expected |= (((w * 4 + s) * 5 + rr + nn) % 8) << (3 * s);
                }
                if (words(w, rr, nn) != expected) {
                    printf("BitFields word (%d, %d, %d) = %d, not %d\n", w, rr, nn, words(w, rr, nn), expected);
                    return 1;
                }
                if (b_out(w, rr, nn) != (w * 7 + rr * 3 + nn) % 16) {
                    printf("BitFields b mismatch at (%d, %d, %d)\n", w, rr, nn);
                    return 1;
                }
            }
            for (int i = 0; i < 8; i++) {
                if (a_out(i, rr, nn) != (i * 5 + rr + nn) % 8) {
                    printf("BitFields a mismatch at (%d, %d, %d)\n", i, rr, nn);
                    return 1;
                }
            }
        }
    }
    return 0;
}

// Planar packing repeats per group: PlanarFieldPack(2, 4) packs each group
// of 16 elements into 4 bytes, byte p holding elements p, p + 4, p + 8 and
// p + 12 of its group; a record of 32 elements is two groups.
int test_repeating_groups() {
    Var e("e"), r("r");
    Func codes("codes");
    codes(e, r) = cast<uint8_t>((e * 3 + r) % 4);
    Approximation planar = PlanarFieldPack(2, 4);
    EncodeResult enc = planar.encode({codes});
    enc.encoded[0].compute_root();
    Buffer<uint8_t> bytes = enc.encoded[0].realize({8, 2});
    Buffer<uint8_t> out = planar.decode(enc.encoded).decoded[0].realize({32, 2});
    for (int rr = 0; rr < 2; rr++) {
        for (int byte = 0; byte < 8; byte++) {
            int group = byte / 4, p = byte % 4, expected = 0;
            for (int s = 0; s < 4; s++) {
                expected |= (((group * 16 + s * 4 + p) * 3 + rr) % 4) << (2 * s);
            }
            if (bytes(byte, rr) != expected) {
                printf("PlanarFieldPack byte (%d, %d) = %d, not %d\n", byte, rr, bytes(byte, rr), expected);
                return 1;
            }
        }
        for (int i = 0; i < 32; i++) {
            if (out(i, rr) != (i * 3 + rr) % 4) {
                printf("PlanarFieldPack mismatch at (%d, %d)\n", i, rr);
                return 1;
            }
        }
    }

    // positions = 1 packs densely, least significant first.
    Approximation dense = PlanarFieldPack(1, 1);
    Func bits("bits");
    bits(e, r) = cast<uint8_t>((e * e + r) % 3 == 0);
    EncodeResult packed = dense.encode({bits});
    packed.encoded[0].compute_root();
    Buffer<uint8_t> dense_bytes = packed.encoded[0].realize({2, 1});
    for (int byte = 0; byte < 2; byte++) {
        int expected = 0;
        for (int s = 0; s < 8; s++) {
            int i = byte * 8 + s;
            expected |= ((i * i) % 3 == 0) << s;
        }
        if (dense_bytes(byte, 0) != expected) {
            printf("Dense byte %d = %d, not %d\n", byte, dense_bytes(byte, 0), expected);
            return 1;
        }
    }
    return 0;
}

int test_invalid_shapes() {
    try {
        BitFields overlapping(UInt(16), {{"a", 4, 2, 0, 4}, {"b", 4, 1, 4}});
        return 1;
    } catch (const CompileError &) {
    }
    try {
        BitFields too_wide(UInt(8), {{"a", 3, 3, 0, 3}});
        return 1;
    } catch (const CompileError &) {
    }
    try {
        PlanarFieldPack uneven(3, 4);
        return 1;
    } catch (const CompileError &) {
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (test_multiple_fields() || test_repeating_groups() || test_invalid_shapes()) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
