#include "Halide.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

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

// As AbsMaxQuantizer, but rank-polymorphic: any dimensions after the block
// index pass through (implicit Vars), and the signature declares no
// dimensionalities.
struct BatchedAbsMaxQuantizer {
    int block, qmax;

    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var kk("kk"), blk("blk");
        RDom r(0, block);
        Func amax("batched_absmax_stat"), scale("batched_absmax_scale"), codes("batched_absmax_codes");
        // An inline reduction, since a pure definition of 0.0f would have no
        // implicit Vars to infer.
        amax(blk, _) = maximum(abs(in[0](r, blk, _)));
        scale(blk, _) = amax(blk, _) / (float)qmax;
        codes(kk, blk, _) = cast<int8_t>(round(in[0](kk, blk, _) / select(scale(blk, _) == 0.0f, 1.0f, scale(blk, _))));
        return {codes, scale};
    }

    std::vector<Func> decode(const std::vector<Func> &encoded) const {
        Var kk("kk"), blk("blk");
        Func out("batched_absmax_decoded");
        out(kk, blk, _) = cast<float>(encoded[0](kk, blk, _)) * encoded[1](blk, _);
        return {out};
    }

    // Contextual, so the dimensionalities follow the input's.
    ApproximationSignature signature(const ApproximationPorts &inputs) const {
        std::optional<int> dims, scale_dims;
        if (inputs.size() == 1 && inputs[0].dimensions) {
            dims = inputs[0].dimensions;
            scale_dims = *dims - 1;
        }
        return {{{"block", Float(32), dims}},
                {{"codes", Int(8), dims, ApproximationRange(-qmax, qmax)}, {"scale", Float(32), scale_dims}}};
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

// The layout units pass trailing (batch) dimensions through unchanged.
int test_batch_dimensions() {
    Var k("k"), n("n"), m("m");

    // BlockReshape: (k, n, m) <-> (kk, blk, n, m), flat and block-indexed.
    Func flat("batched_flat");
    flat(k, n, m) = cast<float>(k + 1000 * n + 10000 * m);
    Approximation reshape = BlockReshape(8);
    EncodeResult blocks = reshape.encode({flat});
    if (blocks.encoded[0].dimensions() != 4) {
        printf("BlockReshape: %d encoded dimensions\n", blocks.encoded[0].dimensions());
        return 1;
    }
    Buffer<float> blocked = blocks.encoded[0].realize({8, 3, 2, 2});
    Buffer<float> unblocked = reshape.decode(blocks.encoded).decoded[0].realize({24, 2, 2});
    for (int mm = 0; mm < 2; mm++) {
        for (int nn = 0; nn < 2; nn++) {
            for (int i = 0; i < 24; i++) {
                float expected = i + 1000 * nn + 10000 * mm;
                if (unblocked(i, nn, mm) != expected || blocked(i % 8, i / 8, nn, mm) != expected) {
                    printf("BlockReshape batch mismatch at (%d, %d, %d)\n", i, nn, mm);
                    return 1;
                }
            }
        }
    }
    Func block_indexed("batched_block_indexed");
    block_indexed(k, n, m) = cast<float>(k + 100 * n + 1000 * m);
    Approximation indexed = BlockReshape({4, 2}, true);
    EncodeResult sub_blocks = indexed.encode({block_indexed});
    Buffer<float> sub = sub_blocks.encoded[0].realize({4, 2, 3, 2});
    Buffer<float> unsub = indexed.decode(sub_blocks.encoded).decoded[0].realize({8, 3, 2});
    for (int mm = 0; mm < 2; mm++) {
        for (int b = 0; b < 3; b++) {
            for (int i = 0; i < 8; i++) {
                float expected = i + 100 * b + 1000 * mm;
                if (unsub(i, b, mm) != expected || sub(i % 4, i / 4, b, mm) != expected) {
                    printf("Block-indexed BlockReshape batch mismatch at (%d, %d, %d)\n", i, b, mm);
                    return 1;
                }
            }
        }
    }

    // PlanarFieldPack: (element, record, n) <-> (position, record, n).
    Var element("element"), record("record");
    Func nibbles("batched_nibbles");
    nibbles(element, record, n) = cast<uint8_t>((element + record + 3 * n) % 16);
    Approximation planar = PlanarFieldPack(4, 8);
    EncodeResult packed = planar.encode({nibbles});
    packed.encoded[0].compute_root();
    Buffer<uint8_t> bytes = packed.encoded[0].realize({8, 2, 3});
    Buffer<uint8_t> fields = planar.decode(packed.encoded).decoded[0].realize({16, 2, 3});
    for (int nn = 0; nn < 3; nn++) {
        for (int r = 0; r < 2; r++) {
            for (int i = 0; i < 16; i++) {
                if (fields(i, r, nn) != (i + r + 3 * nn) % 16) {
                    printf("PlanarFieldPack batch mismatch at (%d, %d, %d)\n", i, r, nn);
                    return 1;
                }
            }
            for (int i = 0; i < 8; i++) {
                int lo = (i + r + 3 * nn) % 16, hi = (i + 8 + r + 3 * nn) % 16;
                if (bytes(i, r, nn) != (lo | (hi << 4))) {
                    printf("PlanarFieldPack batch byte mismatch at (%d, %d, %d)\n", i, r, nn);
                    return 1;
                }
            }
        }
    }

    // StructLayout without a declared record dimensionality takes the inputs'.
    Type record_type = Type::Struct({{"tag", UInt(16)}, {"pixels", UInt(8), 3}});
    Func pixels("batched_pixels"), tag("batched_tag");
    pixels(element, record, n) = cast<uint8_t>(element + 10 * record + 30 * n);
    tag(record, n) = cast<uint16_t>(100 + record + 4 * n);
    Approximation layout = StructLayout(record_type, {"pixels", "tag"});
    EncodeResult records = layout.encode({pixels, tag});
    if (records.encoded[0].dimensions() != 2 || records.encoded_ports[0].dimensions != 2) {
        printf("StructLayout: %d record dimensions\n", records.encoded[0].dimensions());
        return 1;
    }
    DecodeResult unpacked = layout.decode(records.encoded);
    Buffer<uint8_t> out_pixels = unpacked.decoded[0].realize({3, 4, 2});
    Buffer<uint16_t> out_tag = unpacked.decoded[1].realize({4, 2});
    for (int nn = 0; nn < 2; nn++) {
        for (int r = 0; r < 4; r++) {
            if (out_tag(r, nn) != 100 + r + 4 * nn) {
                return 1;
            }
            for (int i = 0; i < 3; i++) {
                if (out_pixels(i, r, nn) != i + 10 * r + 30 * nn) {
                    return 1;
                }
            }
        }
    }

    if (Halide::exceptions_enabled()) {
        // Too few dimensions, or slots that disagree, are errors.
        Func scalar("batched_scalar");
        scalar() = 1.0f;
        try {
            (void)reshape.encode({scalar});
            printf("BlockReshape accepted a 0-D input\n");
            return 1;
        } catch (const CompileError &) {
        }
        Func flat_tag("batched_flat_tag");
        flat_tag(record) = cast<uint16_t>(record);
        try {
            (void)layout.encode({pixels, flat_tag});
            printf("StructLayout accepted slots of different record dimensionalities\n");
            return 1;
        } catch (const CompileError &) {
        }
        try {
            (void)Approximation(StructLayout(record_type, {"pixels", "tag"}, 1)).encode({pixels, tag});
            printf("StructLayout accepted 2-D records with record_dimensions = 1\n");
            return 1;
        } catch (const CompileError &) {
        }
        Func one_pixel("batched_one_pixel"), one_tag("batched_one_tag");
        one_pixel(element) = cast<uint8_t>(element);
        one_tag() = cast<uint16_t>(0);
        try {
            (void)layout.encode({one_pixel, one_tag});
            printf("StructLayout accepted 0-D records\n");
            return 1;
        } catch (const CompileError &) {
        }
    }
    return 0;
}

// One scheme value approximates a row and a matrix of rows: each row of the
// matrix encodes to the same bytes as the row on its own, and a consumer of
// the matrix sees the same decoded values.
int test_batched_scheme() {
    Pointwise offset = Pointwise{"offset",
                                 [](Expr x) { return cast<uint8_t>(x + 8); },
                                 [](Expr x) { return cast<int8_t>(cast<int>(x) - 8); }}
                           .with_types(Int(8), UInt(8))
                           .with_ranges(ApproximationRange(-8, 7), ApproximationRange(0, 15));
    Type q4_type = Type::Struct({{"d", Float(16)}, {"qs", UInt(8), 16}});
    Approximation scheme = Compose(
        BlockReshape{32},
        BatchedAbsMaxQuantizer{32, 7},
        Parallel{{"codes", Compose{offset, PlanarFieldPack{4, 16}}},
                 {"scale", f16_storage()}},
        StructLayout{q4_type, {"qs", "d"}});

    const int K = 64, N = 3;
    Var k("k"), n("n");
    Expr value = sin(cast<float>(k * 7 + n * 13)) * (cast<float>(n) + 1.0f);

    // A matrix, consumed by a matrix-vector product.
    Func W("batched_W"), x("batched_x"), out("batched_out");
    W(k, n) = value;
    x(k) = cos(cast<float>(k));
    RDom r(0, K, "r");
    out(n) = 0.0f;
    out(n) += W(r, n) * x(r);
    ApproximationResult approx = W.approximate_by(scheme, {out});
    for (Func f : approx.intermediates) {
        if (f.has_update_definition() || approx.is_stage_port(f)) {
            f.compute_root();
        }
    }
    if (approx.encoded.size() != 1 || approx.encoded[0].dimensions() != 2 ||
        approx.encoded_ports[0].dimensions != 2) {
        printf("Batched scheme: unexpected encoded shape\n");
        return 1;
    }
    Buffer<> records = approx.encoded[0].realize({K / 32, N});
    Buffer<float> decoded = approx.replacement.realize({K, N});
    Buffer<float> dot = out.realize({N});

    for (int nn = 0; nn < N; nn++) {
        // The same scheme value, on one row.
        Func row("batched_row"), row_copy("batched_row_copy");
        row(k) = substitute(n.name(), nn, value);
        row_copy(k) = row(k);
        ApproximationResult row_approx = row.approximate_by(scheme, {row_copy});
        for (Func f : row_approx.intermediates) {
            if (f.has_update_definition() || row_approx.is_stage_port(f)) {
                f.compute_root();
            }
        }
        Buffer<> row_records = row_approx.encoded[0].realize({K / 32});
        Buffer<float> row_decoded = row_copy.realize({K});
        const size_t row_bytes = (size_t)(K / 32) * q4_type.bytes();
        if (memcmp((const uint8_t *)records.data() + nn * row_bytes, row_records.data(), row_bytes) != 0) {
            printf("Batched scheme: row %d encodes differently\n", nn);
            return 1;
        }
        float expected_dot = 0.0f;
        for (int i = 0; i < K; i++) {
            if (decoded(i, nn) != row_decoded(i)) {
                printf("Batched scheme: decoded (%d, %d) = %g vs %g\n", i, nn, decoded(i, nn), row_decoded(i));
                return 1;
            }
            expected_dot += row_decoded(i) * std::cos((float)i);
        }
        if (std::fabs(dot(nn) - expected_dot) > 1e-4f * (1.0f + std::fabs(expected_dot))) {
            printf("Batched scheme: dot %d = %g vs %g\n", nn, dot(nn), expected_dot);
            return 1;
        }
    }

    // With a context, describe() and check_ranges() see the batch dimension.
    std::string described = scheme.describe({{"values", Float(32), 2}});
    for (const char *expected : {"BlockReshape (values: float32 x2) -> (blocks: float32 x3)",
                                 "PlanarFieldPack (codes: uint8 x3 in [0, 15]) -> (bytes: uint8 x3)",
                                 "StructLayout (bytes: uint8 x3, scale: float16 x2) -> (record: struct{d: float16, qs: uint8[16]} x2)"}) {
        if (described.find(expected) == std::string::npos) {
            printf("describe() is missing \"%s\":\n%s", expected, described.c_str());
            return 1;
        }
    }
    std::vector<std::string> issues = check_ranges(scheme, {{"values", Float(32), 2}});
    if (!issues.empty()) {
        printf("check_ranges: %zu issues\n", issues.size());
        for (const std::string &issue : issues) {
            printf("  %s\n", issue.c_str());
        }
        return 1;
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

// Records whether a vector store of `lanes` lanes to `name` was lowered.
class FindVectorStore : public Internal::IRMutator {
    using IRMutator::visit;
    Internal::Stmt visit(const Internal::Store *op) override {
        if (op->name == name && op->value.type().lanes() == lanes) {
            found = true;
        }
        return IRMutator::visit(op);
    }

public:
    std::string name;
    int lanes = 0;
    bool found = false;
};

class FindAllocation : public Internal::IRMutator {
    using IRMutator::visit;
    Internal::Stmt visit(const Internal::Allocate *op) override {
        if (op->name == name) {
            size = op->constant_allocation_size();
        }
        return IRMutator::visit(op);
    }

public:
    std::string name;
    int32_t size = -1;
};

// A block-indexed decode reads within-block indices as given, so a stage at a
// loop over 4-element pieces of a 32-element block holds only the piece.
int test_block_indexed_pieces() {
    Var j("j"), b("b");
    Func values("piece_values");
    values(j, b) = j + 32 * b;
    Approximation indexed = BlockReshape(32, true);
    Func packed = indexed.encode({values}).encoded[0];
    Func decoded = indexed.decode({packed}).decoded[0];
    Func sum("piece_sum");
    RDom r(0, 32, 0, 4);
    sum() = 0;
    sum() += decoded(r.x, r.y);
    RVar rp("rp"), ri("ri");
    sum.update().split(r.x, rp, ri, 4);
    packed.compute_at(sum, rp);
    FindAllocation finder;
    finder.name = packed.name();
    Pipeline p(sum);
    p.add_custom_lowering_pass(&finder, [] {});
    Buffer<int> out = p.realize();
    if (finder.size != 4) {
        printf("Block-indexed piece of %s: allocated %d elements, expected 4\n", packed.name().c_str(), finder.size);
        return 1;
    }
    if (out() != 127 * 128 / 2) {
        printf("Block-indexed pieces: sum %d, expected %d\n", out(), 127 * 128 / 2);
        return 1;
    }
    return 0;
}

// BlockReshape::tiles splits several leading dimensions into dense tiles:
// (x0, x1, rest...) <-> (x0 % b0, x1 % b1, x0 / b0, x1 / b1, rest...).
int test_block_tiles() {
    Var x("x"), y("y"), z("z"), n("n");

    // 2-D tiles with a pass-through dimension: exact both ways.
    {
        Func f("tiles_2d");
        f(x, y, n) = cast<float>(x + 100 * y + 10000 * n);
        Approximation tiles = BlockReshape::tiles({2, 4});
        EncodeResult e = tiles.encode({f});
        if (e.encoded[0].dimensions() != 5 || e.encoded_ports[0].dimensions != 5 ||
            e.encoded_ports[0].type != Float(32)) {
            printf("tiles({2, 4}): %d encoded dimensions\n", e.encoded[0].dimensions());
            return 1;
        }
        Buffer<float> enc = e.encoded[0].realize({2, 4, 3, 2, 2});
        Buffer<float> dec = tiles.decode(e.encoded).decoded[0].realize({6, 8, 2});
        for (int nn = 0; nn < 2; nn++) {
            for (int yy = 0; yy < 8; yy++) {
                for (int xx = 0; xx < 6; xx++) {
                    float expected = xx + 100 * yy + 10000 * nn;
                    if (dec(xx, yy, nn) != expected ||
                        enc(xx % 2, yy % 4, xx / 2, yy / 4, nn) != expected) {
                        printf("tiles({2, 4}) mismatch at (%d, %d, %d)\n", xx, yy, nn);
                        return 1;
                    }
                }
            }
        }
    }

    // 3-D tiles.
    {
        Func f("tiles_3d");
        f(x, y, z) = cast<int>(x + 100 * y + 10000 * z);
        Approximation tiles = BlockReshape::tiles({2, 3, 2});
        EncodeResult e = tiles.encode({f});
        Buffer<int> enc = e.encoded[0].realize({2, 3, 2, 2, 2, 3});
        Buffer<int> dec = tiles.decode(e.encoded).decoded[0].realize({4, 6, 6});
        for (int zz = 0; zz < 6; zz++) {
            for (int yy = 0; yy < 6; yy++) {
                for (int xx = 0; xx < 4; xx++) {
                    int expected = xx + 100 * yy + 10000 * zz;
                    if (dec(xx, yy, zz) != expected ||
                        enc(xx % 2, yy % 3, zz % 2, xx / 2, yy / 3, zz / 2) != expected) {
                        printf("tiles({2, 3, 2}) mismatch at (%d, %d, %d)\n", xx, yy, zz);
                        return 1;
                    }
                }
            }
        }
    }

    // One tiled dimension is the plain BlockReshape.
    {
        Func f("tiles_1d");
        f(x, n) = cast<float>(x + 1000 * n);
        Buffer<float> tiled = Approximation(BlockReshape::tiles({8})).encode({f}).encoded[0].realize({8, 3, 2});
        Buffer<float> blocked = Approximation(BlockReshape(8)).encode({f}).encoded[0].realize({8, 3, 2});
        if (memcmp(tiled.data(), blocked.data(), tiled.size_in_bytes()) != 0) {
            printf("tiles({8}) differs from BlockReshape(8)\n");
            return 1;
        }
    }

    // Signature: +k dimensions, type and range passed through.
    {
        Approximation tiles = BlockReshape::tiles({2, 2});
        ApproximationPort values("values", UInt(8), 3);
        values.range = ApproximationRange{0, 15};
        std::string described = tiles.describe({values});
        const char *expected = "BlockReshape (values: uint8 x3) -> (blocks: uint8 x5 in [0, 15])";
        if (described.find(expected) == std::string::npos) {
            printf("tiles describe() is missing \"%s\":\n%s", expected, described.c_str());
            return 1;
        }
    }

    // Composes with Parallel, Permute and Pointwise: a tiled matrix and a
    // blocked row, swapped, the matrix stored as float16.
    {
        Func a("tiles_a"), b("tiles_b");
        a(x, y) = cast<float>(x + 16 * y);
        b(x, n) = cast<float>(2 * x - n);
        Pointwise f16{"tiles_f16",
                      [](Expr v) { return cast<float16_t>(v); },
                      [](Expr v) { return cast<float>(v); }};
        Approximation scheme =
            Compose{Parallel{Compose{BlockReshape::tiles({2, 2}), f16}, BlockReshape::tiles({4})},
                    Permute{{1, 0}}};
        EncodeResult e = scheme.encode({a, b});
        if (e.encoded.size() != 2 || e.encoded[0].dimensions() != 3 || e.encoded[1].dimensions() != 4 ||
            e.encoded[1].types()[0] != Float(16)) {
            printf("Composed tiles: unexpected encoded ports\n");
            return 1;
        }
        DecodeResult d = scheme.decode(e.encoded);
        Buffer<float> a_out = d.decoded[0].realize({8, 6});
        Buffer<float> b_out = d.decoded[1].realize({8, 2});
        for (int yy = 0; yy < 6; yy++) {
            for (int xx = 0; xx < 8; xx++) {
                if (a_out(xx, yy) != xx + 16 * yy) {
                    printf("Composed tiles: a(%d, %d) = %g\n", xx, yy, a_out(xx, yy));
                    return 1;
                }
            }
        }
        for (int nn = 0; nn < 2; nn++) {
            for (int xx = 0; xx < 8; xx++) {
                if (b_out(xx, nn) != 2 * xx - nn) {
                    printf("Composed tiles: b(%d, %d) = %g\n", xx, nn, b_out(xx, nn));
                    return 1;
                }
            }
        }
    }

    // The within-tile dimensions of the encoded Func are dense and leading, so
    // a schedule can vectorize across a whole 2x2 tile.
    {
        Func m("tiles_m");
        m(x, y) = x + 100 * y;
        Func packed = Approximation(BlockReshape::tiles({2, 2})).encode({m}).encoded[0];
        std::vector<Var> args = packed.args();
        Var tile("tile");
        packed.bound(args[0], 0, 2).bound(args[1], 0, 2).fuse(args[0], args[1], tile).vectorize(tile);
        // A tile is one dense 4-wide store only if the output's tile rows are adjacent.
        packed.output_buffer().dim(1).set_stride(2);
        FindVectorStore finder;
        finder.name = packed.name();
        finder.lanes = 4;
        Pipeline p(packed);
        p.add_custom_lowering_pass(&finder, [] {});
        Buffer<int> out = p.realize({2, 2, 4, 3});
        if (!finder.found) {
            printf("tiles({2, 2}): no 4-wide store to %s\n", packed.name().c_str());
            return 1;
        }
        for (int yy = 0; yy < 6; yy++) {
            for (int xx = 0; xx < 8; xx++) {
                if (out(xx % 2, yy % 2, xx / 2, yy / 2) != xx + 100 * yy) {
                    printf("Vectorized tiles mismatch at (%d, %d)\n", xx, yy);
                    return 1;
                }
            }
        }
    }

    if (Halide::exceptions_enabled()) {
        Func row("tiles_row");
        row(x) = x;
        for (auto make : {+[] { return BlockReshape::tiles({}); },
                          +[] { return BlockReshape::tiles({2, 0}); }}) {
            try {
                (void)make();
                printf("BlockReshape::tiles accepted bad block extents\n");
                return 1;
            } catch (const CompileError &) {
            }
        }
        try {
            (void)Approximation(BlockReshape::tiles({2, 2})).encode({row});
            printf("BlockReshape::tiles({2, 2}) accepted a 1-D input\n");
            return 1;
        } catch (const CompileError &) {
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
                 {"block-indexed pieces", test_block_indexed_pieces},
                 {"block tiles", test_block_tiles},
                 {"batch dimensions", test_batch_dimensions},
                 {"batched scheme", test_batched_scheme},
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
