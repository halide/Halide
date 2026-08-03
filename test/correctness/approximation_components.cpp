#include "Halide.h"

#include <cmath>
#include <cstdio>

using namespace Halide;

namespace {

int test_struct_layout_1d() {
    Type record_type = Type::Struct({{"d", Float(16)}, {"qh", UInt(8), 4}, {"qs", UInt(8), 16}});
    Var element("element"), record("record");
    Func qs("qs"), qh("qh"), d("d");
    qs(element, record) = cast<uint8_t>(element + 3 * record);
    qh(element, record) = cast<uint8_t>(0x80 + element + record);
    d(record) = cast<float16_t>(cast<float>(record) + 0.5f);

    StructLayout layout(record_type, {"qs", "qh", "d"});
    DecodeResult decoded = layout.decode(layout.encode({qs, qh, d}).encoded);
    Buffer<uint8_t> out_qs = decoded.decoded[0].realize({16, 3});
    Buffer<uint8_t> out_qh = decoded.decoded[1].realize({4, 3});
    Buffer<float16_t> out_d = decoded.decoded[2].realize({3});
    for (int r = 0; r < 3; ++r) {
        if ((float)out_d(r) != r + 0.5f) {
            return 1;
        }
        for (int i = 0; i < 16; ++i) {
            if (out_qs(i, r) != (uint8_t)(i + 3 * r)) {
                return 1;
            }
        }
        for (int i = 0; i < 4; ++i) {
            if (out_qh(i, r) != (uint8_t)(0x80 + i + r)) {
                return 1;
            }
        }
    }
    return 0;
}

int test_struct_layout_2d() {
    Type record_type = Type::Struct({{"tag", UInt(16)}, {"pixels", UInt(8), 3}});
    Var element("element"), x("x"), y("y");
    Func pixels("pixels"), tag("tag");
    pixels(element, x, y) = cast<uint8_t>(element + 10 * x + 30 * y);
    tag(x, y) = cast<uint16_t>(100 + x + 4 * y);
    StructLayout layout(record_type, {"pixels", "tag"}, 2);
    DecodeResult decoded = layout.decode(layout.encode({pixels, tag}).encoded);
    Buffer<uint8_t> out_pixels = decoded.decoded[0].realize({3, 4, 2});
    Buffer<uint16_t> out_tag = decoded.decoded[1].realize({4, 2});
    for (int yy = 0; yy < 2; ++yy) {
        for (int xx = 0; xx < 4; ++xx) {
            if (out_tag(xx, yy) != 100 + xx + 4 * yy) {
                return 1;
            }
            for (int i = 0; i < 3; ++i) {
                if (out_pixels(i, xx, yy) != i + 10 * xx + 30 * yy) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

int test_struct_layout_contract_errors() {
    Type record_type = Type::Struct({{"tag", UInt(16)}, {"pixels", UInt(8), 3}});
    Var element("element"), record("record");
    Func pixels("pixels"), wrong_tag("wrong_tag");
    pixels(element, record) = cast<uint8_t>(element);
    wrong_tag(record) = cast<int16_t>(record);
    try {
        StructLayout layout(record_type, {"pixels", "tag"});
        (void)layout.encode({pixels, wrong_tag});
        return 1;
    } catch (const CompileError &) {
    }
    try {
        StructLayout duplicate(record_type, {"pixels", "pixels"});
        return 1;
    } catch (const CompileError &) {
    }
    return 0;
}

int test_scalar_components() {
    Var record("record");
    Func values("values");
    values(record) = cast<float>(record) / 3.0f;
    StorageCast<float, float16_t> storage;
    EncodeResult stored = storage.encode({values});
    DecodeResult cast_roundtrip = storage.decode(stored.encoded);
    Buffer<float> out = cast_roundtrip.decoded[0].realize({8});
    for (int i = 0; i < 8; ++i) {
        float expected = (float)(float16_t)(i / 3.0f);
        if (out(i) != expected) {
            printf("StorageCast mismatch at %d: %g vs %g\n", i, out(i), expected);
            return 1;
        }
    }

    Func words("words");
    words(record) = cast<uint32_t>((int32_t)0x10203040) + cast<uint32_t>(record);
    LittleEndianScalarPack<uint32_t> little_endian;
    EncodeResult bytes = little_endian.encode({words});
    DecodeResult word_roundtrip = little_endian.decode(bytes.encoded);
    Buffer<uint8_t> packed = bytes.encoded[0].realize({4, 5});
    Buffer<uint32_t> unpacked = word_roundtrip.decoded[0].realize({5});
    for (int r = 0; r < 5; ++r) {
        if (unpacked(r) != 0x10203040u + r || packed(0, r) != (uint8_t)(0x40 + r) ||
            packed(1, r) != 0x30 || packed(2, r) != 0x20 || packed(3, r) != 0x10) {
            printf("LittleEndian mismatch at %d: %08x [%02x %02x %02x %02x]\n", r,
                   unpacked(r), packed(0, r), packed(1, r), packed(2, r), packed(3, r));
            return 1;
        }
    }
    return 0;
}

int test_code_components() {
    Var element("element"), record("record");
    Func high("high");
    high(element, record) = cast<int8_t>(select(((element + record) & 1) != 0, 0, -16));
    BinaryAlphabetPack<int8_t> binary(32, UInt(32), -16, 0);
    EncodeResult word = binary.encode({high});
    word.encoded[0].compute_root();
    DecodeResult expanded = binary.decode(word.encoded);
    Buffer<uint32_t> packed_word = word.encoded[0].realize({2});
    Buffer<int8_t> out_high = expanded.decoded[0].realize({32, 2});
    if (packed_word(0) != 0xaaaaaaaau || packed_word(1) != 0x55555555u) {
        return 1;
    }
    for (int r = 0; r < 2; ++r) {
        for (int i = 0; i < 32; ++i) {
            int8_t expected = ((i + r) & 1) ? 0 : -16;
            if (out_high(i, r) != expected) {
                return 1;
            }
        }
    }

    Func codes("codes");
    codes(element, record) = cast<int8_t>((element % 32) - 16);
    AdditiveRadixSplit split(16, 16);
    EncodeResult parts = split.encode({codes});
    DecodeResult combined = split.decode(parts.encoded);
    Buffer<uint8_t> low = parts.encoded[0].realize({32, 1});
    Buffer<int8_t> high_part = parts.encoded[1].realize({32, 1});
    Buffer<int8_t> combined_codes = combined.decoded[0].realize({32, 1});
    for (int i = 0; i < 32; ++i) {
        if (low(i, 0) != (i & 15) || high_part(i, 0) != (i < 16 ? -16 : 0) ||
            combined_codes(i, 0) != i - 16) {
            return 1;
        }
    }

    PlanarFieldPack planar(4, 16);
    EncodeResult planar_bytes = planar.encode({parts.encoded[0]});
    planar_bytes.encoded[0].compute_root();
    DecodeResult planar_fields = planar.decode(planar_bytes.encoded);
    Buffer<uint8_t> bytes_out = planar_bytes.encoded[0].realize({16, 1});
    Buffer<uint8_t> fields_out = planar_fields.decoded[0].realize({32, 1});
    for (int i = 0; i < 16; ++i) {
        if (bytes_out(i, 0) != (uint8_t)(i | (i << 4)) ||
            fields_out(i, 0) != i || fields_out(i + 16, 0) != i) {
            return 1;
        }
    }
    return 0;
}

int test_block_components() {
    Var k("k");
    Func flat("flat");
    flat(k) = cast<float>(k);
    BlockReshape reshape(32);
    DecodeResult reshaped = reshape.decode(reshape.encode({flat}).encoded);
    Buffer<float> roundtrip = reshaped.decoded[0].realize({96});
    for (int i = 0; i < 96; ++i) {
        if (roundtrip(i) != i) {
            return 1;
        }
    }

    Var element("element"), record("record");
    Func blocks("blocks");
    blocks(element, record) = cast<float>(element - 16);
    SymmetricBlockQuantize quantize(32, 16, BlockRoundingMode::TruncateHalfUpWithOffset,
                                    BlockScaleAnchor::ExtremeSignedValue);
    EncodeResult quantized = quantize.encode({blocks});
    for (Func handle : quantized.handles) {
        handle.compute_root();
    }
    DecodeResult dequantized = quantize.decode(quantized.encoded);
    Buffer<int8_t> codes = quantized.encoded[0].realize({32, 1});
    Buffer<float> scale = quantized.encoded[1].realize({1});
    Buffer<float> values = dequantized.decoded[0].realize({32, 1});
    if (scale(0) != 1.0f) {
        return 1;
    }
    for (int i = 0; i < 32; ++i) {
        if (codes(i, 0) != i - 16 || values(i, 0) != i - 16) {
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    struct Test {
        const char *name;
        int (*run)();
    } tests[] = {{"StructLayout 1-D", test_struct_layout_1d},
                 {"StructLayout 2-D", test_struct_layout_2d},
                 {"StructLayout contract errors", test_struct_layout_contract_errors},
                 {"scalar packs", test_scalar_components},
                 {"code packs", test_code_components},
                 {"block components", test_block_components}};
    for (const Test &test : tests) {
        if (test.run()) {
            printf("Approximation component test failed: %s\n", test.name);
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
