#include "Halide.h"

#include <cmath>
#include <cstdio>

using namespace Halide;

namespace {

// A per-block absmax int8 quantizer on (within, block) floats: codes in
// [-qmax, qmax] and one float scale per block.
struct AbsMaxQuantizer {
    int block, qmax;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var kk("kk"), blk("blk");
        RDom r(0, block);
        Func amax("absmax_stat"), scale("absmax_scale"), codes("absmax_codes");
        amax(blk) = 0.0f;
        amax(blk) = max(amax(blk), abs(in[0](r, blk)));
        scale(blk) = amax(blk) / (float)qmax;
        codes(kk, blk) = cast<int8_t>(round(in[0](kk, blk) / select(scale(blk) == 0.0f, 1.0f, scale(blk))));
        return {codes, scale};
    }

    std::vector<Func> decode(const std::vector<Func> &encoded) const {
        Var kk("kk"), blk("blk");
        Func out("absmax_decoded");
        out(kk, blk) = cast<float>(encoded[0](kk, blk)) * encoded[1](blk);
        return {out};
    }

    ApproximationSignature signature() const {
        return {{{"block", Float(32), 2}}, {{"codes", Int(8), 2}, {"scale", Float(32), 1}}};
    }
};

// Reduced-precision storage: a cast to and from float16.
Pointwise f16_storage() {
    return Pointwise{"f16",
                     [](Expr x) { return strict_float(cast<float16_t>(x)); },
                     [](Expr x) { return strict_float(cast<float>(x)); }};
}

int test_struct_layout_1d() {
    Type record_type = Type::Struct({{"d", Float(16)}, {"qh", UInt(8), 4}, {"qs", UInt(8), 16}});
    Var element("element"), record("record");
    Func qs("qs"), qh("qh"), d("d");
    qs(element, record) = cast<uint8_t>(element + 3 * record);
    qh(element, record) = cast<uint8_t>(0x80 + element + record);
    d(record) = cast<float16_t>(cast<float>(record) + 0.5f);

    Approximation layout = StructLayout(record_type, {"qs", "qh", "d"});
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
    Approximation layout = StructLayout(record_type, {"pixels", "tag"}, 2);
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
    if (!Halide::exceptions_enabled()) {
        return 0;
    }
    Type record_type = Type::Struct({{"tag", UInt(16)}, {"pixels", UInt(8), 3}});
    Var element("element"), record("record");
    Func pixels("pixels"), wrong_tag("wrong_tag");
    pixels(element, record) = cast<uint8_t>(element);
    wrong_tag(record) = cast<int16_t>(record);
    try {
        Approximation layout = StructLayout(record_type, {"pixels", "tag"});
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
    Approximation storage = f16_storage();
    EncodeResult stored = storage.encode({values});
    DecodeResult cast_roundtrip = storage.decode(stored.encoded);
    Buffer<float> out = cast_roundtrip.decoded[0].realize({8});
    for (int i = 0; i < 8; ++i) {
        float expected = (float)(float16_t)(i / 3.0f);
        if (out(i) != expected) {
            printf("f16 cast mismatch at %d: %g vs %g\n", i, out(i), expected);
            return 1;
        }
    }

    Func words("words");
    words(record) = cast<uint32_t>((int32_t)0x10203040) + cast<uint32_t>(record);
    Approximation little_endian = LittleEndianScalarPack<uint32_t>{};
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
    Func signed_nibbles("signed_nibbles");
    signed_nibbles(element, record) = cast<int8_t>((element % 16) - 8);
    Approximation offset = Pointwise{"offset",
                                     [](Expr x) { return cast<uint8_t>(x + 8); },
                                     [](Expr x) { return cast<int8_t>(x - 8); }};
    EncodeResult offset_codes = offset.encode({signed_nibbles});
    DecodeResult signed_roundtrip = offset.decode(offset_codes.encoded);
    Buffer<uint8_t> stored_nibbles = offset_codes.encoded[0].realize({16, 2});
    Buffer<int8_t> restored_nibbles = signed_roundtrip.decoded[0].realize({16, 2});
    for (int r = 0; r < 2; ++r) {
        for (int i = 0; i < 16; ++i) {
            if (stored_nibbles(i, r) != i || restored_nibbles(i, r) != i - 8) {
                return 1;
            }
        }
    }

    Func nibbles("nibbles");
    nibbles(element, record) = cast<uint8_t>(element % 16);
    Approximation planar = PlanarFieldPack(4, 16);
    EncodeResult planar_bytes = planar.encode({nibbles});
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
    Approximation reshape = BlockReshape(32);
    DecodeResult reshaped = reshape.decode(reshape.encode({flat}).encoded);
    Buffer<float> roundtrip = reshaped.decoded[0].realize({96});
    for (int i = 0; i < 96; ++i) {
        if (roundtrip(i) != i) {
            return 1;
        }
    }

    return 0;
}

int test_standard_quant_compositions() {
    Var k("k");

    Pointwise offset{"offset",
                     [](Expr x) { return cast<uint8_t>(x + 8); },
                     [](Expr x) { return cast<int8_t>(x - 8); }};
    Type q4_type = Type::Struct({{"d", Float(16)}, {"qs", UInt(8), 16}});
    Func q4_values("q4_values");
    q4_values(k) = cast<float>((k % 15) - 7);
    Approximation q4 = Compose(
        BlockReshape{32},
        AbsMaxQuantizer{32, 7},
        Parallel{{"codes", Compose{offset, PlanarFieldPack{4, 16}}},
                 {"scale", f16_storage()}},
        StructLayout{q4_type, {"qs", "d"}});
    EncodeResult q4_encoded = q4.encode({q4_values});
    for (Func intermediate : q4_encoded.intermediates) {
        intermediate.compute_root();
    }
    DecodeResult q4_decoded = q4.decode(q4_encoded.encoded);
    Buffer<float> q4_roundtrip = q4_decoded.decoded[0].realize({64});
    for (int i = 0; i < 64; ++i) {
        if (q4_roundtrip(i) != (i % 15) - 7) {
            return 1;
        }
    }

    Type q8_type = Type::Struct({{"d", Float(16)}, {"qs", Int(8), 32}});
    Func q8_values("q8_values");
    Expr local = k % 32;
    q8_values(k) = cast<float>(select(local == 0, -127, local - 16));
    Approximation q8 = Compose(
        BlockReshape{32},
        AbsMaxQuantizer{32, 127},
        Parallel{Identity{}, f16_storage()},
        StructLayout{q8_type, {"qs", "d"}});
    EncodeResult q8_encoded = q8.encode({q8_values});
    for (Func intermediate : q8_encoded.intermediates) {
        intermediate.compute_root();
    }
    DecodeResult q8_decoded = q8.decode(q8_encoded.encoded);
    Buffer<float> q8_roundtrip = q8_decoded.decoded[0].realize({64});
    for (int i = 0; i < 64; ++i) {
        int local_i = i % 32;
        float expected = local_i == 0 ? -127.0f : local_i - 16.0f;
        if (q8_roundtrip(i) != expected) {
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
                 {"block components", test_block_components},
                 {"standard quant compositions", test_standard_quant_compositions}};
    for (const Test &test : tests) {
        if (test.run()) {
            printf("Approximation component test failed: %s\n", test.name);
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
