#include "Halide.h"
#include <cmath>
#include <cstring>
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// A block_q4_0-shaped struct, per doc/StructTypeDesign.md: one float16 scale
// followed by 16 packed byte-wide "codes" (simplified here to not bother with
// block_q4_0's own nibble-packing trick -- that's a detail of the format, not
// of the struct-type mechanism this test is actually checking).
Type make_block_type() {
    return Type::Struct({{"d", Float(16)}, {"qs", UInt(8), 16}});
}

void test_type_struct_basics() {
    Type block_a = make_block_type();
    Type block_b = make_block_type();  // independently constructed, same layout
    Type reordered = Type::Struct({{"qs", UInt(8), 16}, {"d", Float(16)}});
    Type different_field = Type::Struct({{"d", Float(16)}, {"qs", UInt(8), 8}});

    if (!block_a.is_struct() || !block_b.is_struct()) {
        printf("Expected Type::Struct() results to report is_struct()\n");
        exit(1);
    }
    if (block_a.bytes() != 18) {
        printf("Expected block_a.bytes() == 18, got %d\n", block_a.bytes());
        exit(1);
    }
    if (block_a.code() != Type::UInt || block_a.bits() != 8 || block_a.lanes() != 1) {
        printf("Expected a struct type's runtime tag to be plain UInt(8)\n");
        exit(1);
    }
    if (!(block_a == block_b)) {
        printf("Two independently-declared-but-identical struct types should compare equal\n");
        exit(1);
    }
    if (block_a == reordered) {
        printf("Struct types with differently-ordered fields should not compare equal\n");
        exit(1);
    }
    if (block_a == different_field) {
        printf("Struct types with different field shapes should not compare equal\n");
        exit(1);
    }
    if (block_a == UInt(8)) {
        printf("A struct type should not compare equal to a plain UInt(8)\n");
        exit(1);
    }
    // with_bits/with_code/with_lanes must preserve struct_type when the
    // requested change is a no-op (e.g. PromoteToMemoryType's
    // t.with_bits(((t.bits()+7)/8)*8) in StorageFlattening.cpp, which is a
    // no-op for a struct type since its bits() is already 8) -- otherwise a
    // struct-typed Load/Store would silently degrade into a plain UInt8 one
    // partway through lowering.
    if (!block_a.with_bits(8).is_struct()) {
        printf("Type::with_bits(same bits) should preserve struct-ness\n");
        exit(1);
    }
    if (block_a.with_bits(16).is_struct()) {
        printf("Type::with_bits(different bits) should drop struct-ness\n");
        exit(1);
    }
    if (!block_a.with_code(Type::UInt).is_struct()) {
        printf("Type::with_code(same code) should preserve struct-ness\n");
        exit(1);
    }
    if (!block_a.with_lanes(1).is_struct()) {
        printf("Type::with_lanes(same lanes) should preserve struct-ness\n");
        exit(1);
    }
}

// operator< must agree with operator==: two independently-allocated but
// structurally-identical struct types (block_a/block_b, from two separate
// Type::Struct() calls) carry different StructTypeInfo pointers, so a
// pointer-based operator< would report them as ordered relative to each
// other despite comparing equal, breaking the usual invariant that
// !(a < b) && !(b < a) iff a == b (which std::map/std::set/std::sort all
// rely on).
void test_struct_type_ordering_consistent_with_equality() {
    Type block_a = make_block_type();
    Type block_b = make_block_type();  // independently constructed, same layout
    Type reordered = Type::Struct({{"qs", UInt(8), 16}, {"d", Float(16)}});

    if (block_a < block_b || block_b < block_a) {
        printf("Equal struct types must not be ordered relative to each other\n");
        exit(1);
    }
    if (!(block_a < reordered) && !(reordered < block_a)) {
        printf("Unequal struct types must be ordered one way or the other\n");
        exit(1);
    }
    if (block_a < reordered && reordered < block_a) {
        printf("Struct type ordering must be antisymmetric\n");
        exit(1);
    }
    if (UInt(8) < block_a && block_a < UInt(8)) {
        printf("Struct-vs-non-struct ordering must be antisymmetric\n");
        exit(1);
    }
}

// Reading struct-typed data through an ImageParam/hand-built Buffer. A
// struct-typed ImageParam/Func keeps *exactly* the dimensionality the user
// declared -- no hidden extra dimension anywhere -- so its buffer shape is
// built exactly like any other typed buffer's would be, with strides in
// units of "one whole struct" (Type::bytes() is what scales that up to an
// actual byte address, entirely inside field()'s lowering).
void test_read_from_buffer() {
    Type block_t = make_block_type();
    const int num_blocks = 5, num_rows = 3;

    int block_bytes = block_t.bytes();
    std::vector<uint8_t> data((size_t)block_bytes * num_blocks * num_rows);
    auto at = [&](int byte, int blk, int row) -> uint8_t & {
        return data[(size_t)byte + blk * block_bytes + row * block_bytes * num_blocks];
    };
    for (int row = 0; row < num_rows; row++) {
        for (int blk = 0; blk < num_blocks; blk++) {
            float16_t d_val(1.5f + 0.25f * blk + 0.1f * row);
            uint16_t bits = d_val.to_bits();
            memcpy(&at(0, blk, row), &bits, 2);
            for (int k = 0; k < 16; k++) {
                at(2 + k, blk, row) = (uint8_t)((blk * 3 + k + row) % 251);
            }
        }
    }
    // Exactly 2 dims (blk, row), matching Wt's own declared dimensionality --
    // strides are in units of "one struct" (dense AoS: blk's stride is 1,
    // row's stride is num_blocks), the same convention any other typed
    // buffer uses regardless of its element's byte size.
    halide_dimension_t shape[2] = {
        {0, num_blocks, 1, 0},
        {0, num_rows, num_blocks, 0},
    };
    Buffer<uint8_t> Wt_buf(data.data(), 2, shape);

    ImageParam Wt(block_t, 2, "Wt");
    Wt.set(Wt_buf);

    Var k("k"), blk("blk"), row("row");

    Func delta("delta");
    delta(blk, row) = cast<float>(field(Wt(blk, row), "d"));

    Func code("code");
    code(k, blk, row) = cast<int32_t>(field(Wt(blk, row), "qs")[k]);

    Buffer<float> delta_result = delta.realize({num_blocks, num_rows});
    Buffer<int32_t> code_result = code.realize({16, num_blocks, num_rows});

    for (int row = 0; row < num_rows; row++) {
        for (int blk = 0; blk < num_blocks; blk++) {
            float16_t expected_d(1.5f + 0.25f * blk + 0.1f * row);
            float expected = (float)expected_d;
            if (delta_result(blk, row) != expected) {
                printf("delta(%d, %d) = %f instead of %f\n", blk, row, delta_result(blk, row), expected);
                exit(1);
            }
            for (int k = 0; k < 16; k++) {
                int32_t expected_code = (blk * 3 + k + row) % 251;
                if (code_result(k, blk, row) != expected_code) {
                    printf("code(%d, %d, %d) = %d instead of %d\n",
                           k, blk, row, code_result(k, blk, row), expected_code);
                    exit(1);
                }
            }
        }
    }
}

// A struct-valued Func that IS the pipeline's own output, realized directly.
// Struct types are treated as an ordinary opaque element type all the way
// through scheduling/bounds_inference/storage_flattening -- producer keeps
// its declared dimensionality (1, just `blk`) throughout, and Pipeline's own
// realize() needs no struct-specific handling: it allocates a struct-typed,
// 1-dimensional output buffer exactly like it would for any other type.
void test_realized_writer_as_output() {
    Type block_t = make_block_type();
    Var blk("blk");
    const int num_blocks = 6;

    Func producer("producer");
    std::vector<Expr> field_values;
    field_values.push_back(cast<float16_t>(0.5f + cast<float>(blk)));  // d
    for (int i = 0; i < 16; i++) {
        field_values.push_back(cast<uint8_t>(blk * 5 + i));  // qs[i]
    }
    producer(blk) = pack_struct(block_t, field_values);

    Buffer<> raw = producer.realize({num_blocks});
    // raw.type() can't self-report struct-ness -- Halide::Buffer<>::type()
    // round-trips through the raw ABI-level halide_type_t, which carries no
    // struct_type side information at all (the same documented limitation
    // Handle-typed buffers already have). Dimensionality is still exactly
    // what producer declared (1, just `blk`) -- no hidden extra dimension.
    if (raw.dimensions() != 1 || raw.dim(0).extent() != num_blocks) {
        printf("Unexpected realized struct output shape: %d dims, dim0 extent %d\n",
               raw.dimensions(), raw.dim(0).extent());
        exit(1);
    }

    // Read it back through a fresh struct-typed ImageParam wrapping the same
    // buffer directly -- no coercion needed, both sides agree on both type
    // and dimensionality.
    ImageParam Wt(block_t, 1, "Wt2");
    Wt.set(raw);
    Var blk2("blk2");
    Func delta("delta");
    delta(blk2) = cast<float>(field(Wt(blk2), "d"));
    Func code0("code0");
    code0(blk2) = cast<int32_t>(field(Wt(blk2), "qs")[0]);

    Buffer<float> delta_result = delta.realize({num_blocks});
    Buffer<int32_t> code0_result = code0.realize({num_blocks});
    for (int blk_i = 0; blk_i < num_blocks; blk_i++) {
        float16_t expected_d(0.5f + (float)blk_i);
        if (delta_result(blk_i) != (float)expected_d) {
            printf("realized writer: delta(%d) = %f instead of %f\n",
                   blk_i, delta_result(blk_i), (float)expected_d);
            exit(1);
        }
        int32_t expected_code0 = (uint8_t)(blk_i * 5);
        if (code0_result(blk_i) != expected_code0) {
            printf("realized writer: code0(%d) = %d instead of %d\n",
                   blk_i, code0_result(blk_i), expected_code0);
            exit(1);
        }
    }
}

std::vector<Expr> block_field_values(const Var &blk, float d_offset, int code_mul) {
    std::vector<Expr> vals;
    vals.push_back(cast<float16_t>(d_offset + cast<float>(blk)));
    for (int i = 0; i < 16; i++) {
        vals.push_back(cast<uint8_t>(blk * code_mul + i));
    }
    return vals;
}

float expected_d(float d_offset, int blk) {
    return (float)float16_t(d_offset + (float)blk);
}

// Torture test: a struct-valued Func consumed *directly* by another Func in
// the same pipeline (not realized standalone, not read back via ImageParam).
// This is exactly the case that broke bounds_inference under the old
// "desugar into a hidden extra dimension" architecture -- producer here
// keeps its true declared dimensionality (1) throughout, so there's nothing
// unusual for bounds_inference to trip on.
void test_struct_consumed_by_another_func() {
    Type block_t = make_block_type();
    Var blk("blk");
    const int num_blocks = 6;

    Func producer("producer");
    producer(blk) = pack_struct(block_t, block_field_values(blk, 0.5f, 5));
    producer.compute_root();

    Func consumer("consumer");
    consumer(blk) = cast<float>(field(producer(blk), "d"));

    Buffer<float> result = consumer.realize({num_blocks});
    for (int i = 0; i < num_blocks; i++) {
        float expected = expected_d(0.5f, i);
        if (result(i) != expected) {
            printf("struct_consumed_by_another_func: consumer(%d) = %f instead of %f\n", i, result(i), expected);
            exit(1);
        }
    }
}

// Torture test: compute_at a struct-valued producer inside a consumer's
// loop, rather than compute_root or fully inlining it.
void test_compute_at() {
    Type block_t = make_block_type();
    Var blk("blk"), blk_o("blk_o"), blk_i("blk_i");
    const int num_blocks = 8;

    Func producer("producer");
    producer(blk) = pack_struct(block_t, block_field_values(blk, 1.0f, 1));

    Func consumer("consumer");
    consumer(blk) = cast<float>(field(producer(blk), "d")) +
                    cast<float>(field(producer(blk), "qs")[0]);
    consumer.split(blk, blk_o, blk_i, 4);
    producer.compute_at(consumer, blk_o);

    Buffer<float> result = consumer.realize({num_blocks});
    for (int i = 0; i < num_blocks; i++) {
        float expected = expected_d(1.0f, i) + (float)(uint8_t)(i * 1);
        if (std::abs(result(i) - expected) > 1e-3f) {
            printf("compute_at: consumer(%d) = %f instead of %f\n", i, result(i), expected);
            exit(1);
        }
    }
}

// Torture test: one struct-valued intermediate consumed by two different
// downstream Funcs, reading different fields with different schedules
// (scalar vs. vectorized). Confirms both share the same producer bounds
// without either schedule interfering with the other's view of the struct.
void test_two_consumers_different_schedules() {
    Type block_t = make_block_type();
    Var blk("blk"), k("k");
    const int num_blocks = 8;

    Func producer("producer");
    producer(blk) = pack_struct(block_t, block_field_values(blk, 2.0f, 7));
    producer.compute_root();

    Func delta("delta");
    delta(blk) = cast<float>(field(producer(blk), "d"));

    Func codes("codes");
    codes(k, blk) = cast<int32_t>(field(producer(blk), "qs")[k]);
    codes.bound(k, 0, 16).vectorize(k, 16);

    Buffer<float> delta_result = delta.realize({num_blocks});
    Buffer<int32_t> codes_result = codes.realize({16, num_blocks});
    for (int i = 0; i < num_blocks; i++) {
        float expected = expected_d(2.0f, i);
        if (delta_result(i) != expected) {
            printf("two_consumers: delta(%d) = %f instead of %f\n", i, delta_result(i), expected);
            exit(1);
        }
        for (int k_i = 0; k_i < 16; k_i++) {
            int32_t expected_code = (uint8_t)(i * 7 + k_i);
            if (codes_result(k_i, i) != expected_code) {
                printf("two_consumers: codes(%d, %d) = %d instead of %d\n",
                       k_i, i, codes_result(k_i, i), expected_code);
                exit(1);
            }
        }
    }
}

// Torture test: a struct-valued Func's own value is a bare passthrough Call
// to another struct-valued Func (e.g. via Func::in()), not a literal
// pack_struct() -- this is what a scheduling wrapper looks like in
// practice, and it must be treated as a valid struct-typed Store value (a
// whole-struct copy split field-by-field) rather than only recognizing a
// literal pack_struct().
void test_passthrough_wrapper() {
    Type block_t = make_block_type();
    Var blk("blk");
    const int num_blocks = 5;

    Func producer("producer");
    producer(blk) = pack_struct(block_t, block_field_values(blk, 3.0f, 3));
    producer.compute_root();

    Func wrapper = producer.in();
    wrapper.compute_root();

    Func consumer("consumer");
    consumer(blk) = cast<float>(field(wrapper(blk), "d")) +
                    cast<float>(field(wrapper(blk), "qs")[2]);

    Buffer<float> result = consumer.realize({num_blocks});
    for (int i = 0; i < num_blocks; i++) {
        float expected = expected_d(3.0f, i) + (float)(uint8_t)(i * 3 + 2);
        if (std::abs(result(i) - expected) > 1e-3f) {
            printf("passthrough_wrapper: consumer(%d) = %f instead of %f\n", i, result(i), expected);
            exit(1);
        }
    }
}

// Torture test: a struct-valued Func's value is a select() between two
// struct-typed Exprs, and the Func is compute_root'd -- so the Store
// splitter must push field projection through the Select, not just
// recognize a literal pack_struct() directly as the whole value.
void test_select_between_structs() {
    Type block_t = make_block_type();
    Var blk("blk");
    const int num_blocks = 6;

    Func producer("producer");
    Expr cond = (blk % 2) == 0;
    producer(blk) = select(cond,
                           pack_struct(block_t, block_field_values(blk, 10.0f, 1)),
                           pack_struct(block_t, block_field_values(blk, 20.0f, 1)));
    producer.compute_root();

    Func consumer("consumer");
    consumer(blk) = cast<float>(field(producer(blk), "d"));

    Buffer<float> result = consumer.realize({num_blocks});
    for (int i = 0; i < num_blocks; i++) {
        float expected = (i % 2 == 0) ? expected_d(10.0f, i) : expected_d(20.0f, i);
        if (result(i) != expected) {
            printf("select_between_structs: consumer(%d) = %f instead of %f\n", i, result(i), expected);
            exit(1);
        }
    }
}

// Torture test: a struct-typed sub-expression used twice within one Func's
// value gets hoisted into a Let by common_subexpression_elimination.
// field()'s lowering must resolve a struct-typed Let-bound Variable back to
// the real (buffer-backed, in this case) value rather than erroring out.
void test_cse_shared_struct_let() {
    Type block_t = make_block_type();
    Var blk("blk");
    const int num_blocks = 6;

    Func producer("producer");
    producer(blk) = pack_struct(block_t, block_field_values(blk, 1.0f, 7));
    producer.compute_root();

    Func result("result");
    Expr blk_struct = producer(blk);
    result(blk) = cast<float>(field(blk_struct, "d")) + cast<float>(field(blk_struct, "qs")[0]);

    Buffer<float> r = result.realize({num_blocks});
    for (int i = 0; i < num_blocks; i++) {
        float expected = expected_d(1.0f, i) + (float)(uint8_t)(i * 7);
        if (std::abs(r(i) - expected) > 1e-3f) {
            printf("cse_shared_struct_let: result(%d) = %f instead of %f\n", i, r(i), expected);
            exit(1);
        }
    }
}

// Torture test: an array field read with a *runtime*, vectorized index --
// must lower to ordinary byte-addressed loads that the normal vectorizer
// (which runs well after struct lowering) can still turn into wide loads,
// not a per-lane select-chain or scalar loop.
void test_vectorized_array_index() {
    Type block_t = make_block_type();
    Var blk("blk"), k("k");
    const int num_blocks = 4;

    Func producer("producer");
    producer(blk) = pack_struct(block_t, block_field_values(blk, 4.0f, 9));
    producer.compute_root();

    Func codes("codes");
    codes(k, blk) = cast<int32_t>(field(producer(blk), "qs")[k]);
    codes.bound(k, 0, 16).vectorize(k, 16);

    Buffer<int32_t> result = codes.realize({16, num_blocks});
    for (int blk_i = 0; blk_i < num_blocks; blk_i++) {
        for (int k_i = 0; k_i < 16; k_i++) {
            int32_t expected = (uint8_t)(blk_i * 9 + k_i);
            if (result(k_i, blk_i) != expected) {
                printf("vectorized_array_index: codes(%d, %d) = %d instead of %d\n",
                       k_i, blk_i, result(k_i, blk_i), expected);
                exit(1);
            }
        }
    }
}

// Torture test: a struct field whose own type is itself Type::Struct (a
// sub-struct field), read through two chained front-end field() calls. Only
// supported for the fully-inlined case (see test/error/struct_nested_field_store.cpp
// and test/error/struct_nested_field_load.cpp for the unsupported,
// buffer-backed cases) -- this test exercises the supported, inlined path.
void test_nested_struct() {
    Type inner_t = Type::Struct({{"a", Int(32)}, {"b", UInt(8)}});
    Type outer_t = Type::Struct({{"inner", inner_t}, {"c", Float(32)}});
    Var i("i");
    const int n = 4;

    Func producer("producer");
    producer(i) = pack_struct(outer_t, {pack_struct(inner_t, {cast<int32_t>(i * 10), cast<uint8_t>(i)}), cast<float>(i) + 0.5f});
    // Left at the default (inlined) schedule: this is the supported path.

    Func consumer("consumer");
    Expr outer_val = producer(i);
    Expr inner_val = field(outer_val, "inner");
    consumer(i) = field(inner_val, "a") + cast<int32_t>(field(inner_val, "b")) + cast<int32_t>(field(outer_val, "c"));

    Buffer<int32_t> result = consumer.realize({n});
    for (int idx = 0; idx < n; idx++) {
        int32_t expected = idx * 10 + idx + (int32_t)(idx + 0.5f);
        if (result(idx) != expected) {
            printf("nested_struct: consumer(%d) = %d instead of %d\n", idx, result(idx), expected);
            exit(1);
        }
    }
}

// Torture test: a struct-typed sub-expression that is itself a nested struct
// field (not a top-level struct value), used twice within its enclosing
// pack_struct's own definition -- forces common_subexpression_elimination to
// hoist it into a struct-typed Let whose *body* refers to it as a bare
// Variable directly inside another pack_struct's argument list (not through
// field()). This exercises LowerStructTypesMutator's generic struct-typed
// Variable resolution path (visit(Variable)), not just project_field's own
// internal Let/Variable handling.
void test_nested_struct_cse_shared_inner() {
    Type inner_t = Type::Struct({{"a", Int(32)}, {"b", UInt(8)}});
    Type outer_t = Type::Struct({{"inner1", inner_t}, {"inner2", inner_t}});
    Var i("i");
    const int n = 5;

    Func producer("producer");
    Expr shared_inner = pack_struct(inner_t, {cast<int32_t>(i * 10), cast<uint8_t>(i)});
    producer(i) = pack_struct(outer_t, {shared_inner, shared_inner});

    Func consumer("consumer");
    Expr outer_val = producer(i);
    Expr inner1 = field(outer_val, "inner1");
    Expr inner2 = field(outer_val, "inner2");
    consumer(i) = field(inner1, "a") + cast<int32_t>(field(inner1, "b")) +
                  field(inner2, "a") + cast<int32_t>(field(inner2, "b"));

    Buffer<int32_t> result = consumer.realize({n});
    for (int idx = 0; idx < n; idx++) {
        int32_t expected = 2 * (idx * 10 + idx);
        if (result(idx) != expected) {
            printf("nested_struct_cse_shared_inner: consumer(%d) = %d instead of %d\n", idx, result(idx), expected);
            exit(1);
        }
    }
}

// Torture test: an ImageParam over a non-dense (padded/strided) layout --
// struct field addressing must compose with an arbitrary declared stride,
// not just the dense case exercised by test_read_from_buffer.
void test_non_dense_stride() {
    Type block_t = make_block_type();
    const int num_blocks = 4, row_stride_in_blocks = 6;  // 2 extra padding blocks per row
    int block_bytes = block_t.bytes();

    std::vector<uint8_t> data((size_t)block_bytes * row_stride_in_blocks);
    auto at = [&](int byte, int blk) -> uint8_t & { return data[(size_t)byte + blk * block_bytes]; };
    for (int blk = 0; blk < num_blocks; blk++) {
        float16_t d_val(7.0f + blk);
        uint16_t bits = d_val.to_bits();
        memcpy(&at(0, blk), &bits, 2);
    }
    halide_dimension_t shape[1] = {
        {0, num_blocks, 1, 0},
    };
    Buffer<uint8_t> Wt_buf(data.data(), 1, shape);

    ImageParam Wt(block_t, 1, "WtPadded");
    Wt.set(Wt_buf);

    Var blk("blk");
    Func delta("delta");
    delta(blk) = cast<float>(field(Wt(blk), "d"));

    Buffer<float> result = delta.realize({num_blocks});
    for (int i = 0; i < num_blocks; i++) {
        float expected = (float)float16_t(7.0f + i);
        if (result(i) != expected) {
            printf("non_dense_stride: delta(%d) = %f instead of %f\n", i, result(i), expected);
            exit(1);
        }
    }
}

// Torture test: a struct-valued Func left at its default (compute_inline)
// schedule and consumed by another Func -- must still fold away to nothing
// materialized, matching the doc's "no bytes ever touched for an inlined
// struct producer" claim.
void test_inlined_default_schedule() {
    Type block_t = make_block_type();
    Var blk("blk");
    const int num_blocks = 6;

    Func producer("producer");
    producer(blk) = pack_struct(block_t, block_field_values(blk, 6.0f, 2));
    // No explicit schedule -- defaults to compute_inline().

    Func consumer("consumer");
    consumer(blk) = cast<float>(field(producer(blk), "d"));

    Buffer<float> result = consumer.realize({num_blocks});
    for (int i = 0; i < num_blocks; i++) {
        float expected = expected_d(6.0f, i);
        if (result(i) != expected) {
            printf("inlined_default_schedule: consumer(%d) = %f instead of %f\n", i, result(i), expected);
            exit(1);
        }
    }
}

// Torture test: .parallel() on a compute_root'd struct producer's logical
// dim. Each parallel iteration splits its own point's struct value into N
// field-stores at disjoint byte ranges, so there's no cross-iteration
// hazard from the splitting itself.
void test_parallel_schedule() {
    Type block_t = make_block_type();
    Var blk("blk");
    const int num_blocks = 32;

    Func producer("producer");
    producer(blk) = pack_struct(block_t, block_field_values(blk, 8.0f, 11));
    producer.compute_root().parallel(blk);

    Func consumer("consumer");
    consumer(blk) = cast<float>(field(producer(blk), "d")) +
                    cast<float>(field(producer(blk), "qs")[15]);

    Buffer<float> result = consumer.realize({num_blocks});
    for (int i = 0; i < num_blocks; i++) {
        float expected = expected_d(8.0f, i) + (float)(uint8_t)(i * 11 + 15);
        if (std::abs(result(i) - expected) > 1e-3f) {
            printf("parallel_schedule: consumer(%d) = %f instead of %f\n", i, result(i), expected);
            exit(1);
        }
    }
}

// Torture test: an array field read with a *runtime* index directly off of
// an inlined (never materialized) struct producer, so field()'s lowering
// sees a literal pack_struct() rather than a Load -- this exercises
// fold_field_of_pack's runtime-index select-chain fallback (as opposed to
// its constant-index fast path, which every other array-field test here
// takes because their producer is compute_root'd/compute_at'd first).
void test_runtime_index_into_inlined_pack() {
    Type block_t = make_block_type();
    Var blk("blk"), k("k");
    const int num_blocks = 3;

    Func producer("producer");
    producer(blk) = pack_struct(block_t, block_field_values(blk, 0.0f, 4));
    // No explicit schedule -- stays compute_inline(), so field() below sees
    // the literal pack_struct() directly instead of a buffer Load.

    Func codes("codes");
    codes(k, blk) = cast<int32_t>(field(producer(blk), "qs")[k]);
    // k is a runtime (non-constant) index into the array field.

    Buffer<int32_t> result = codes.realize({16, num_blocks});
    for (int blk_i = 0; blk_i < num_blocks; blk_i++) {
        for (int k_i = 0; k_i < 16; k_i++) {
            int32_t expected = (uint8_t)(blk_i * 4 + k_i);
            if (result(k_i, blk_i) != expected) {
                printf("runtime_index_into_inlined_pack: codes(%d, %d) = %d instead of %d\n",
                       k_i, blk_i, result(k_i, blk_i), expected);
                exit(1);
            }
        }
    }
}

}  // namespace

int main(int argc, char **argv) {
    test_type_struct_basics();
    test_struct_type_ordering_consistent_with_equality();
    test_read_from_buffer();
    test_realized_writer_as_output();
    test_struct_consumed_by_another_func();
    test_compute_at();
    test_two_consumers_different_schedules();
    test_passthrough_wrapper();
    test_select_between_structs();
    test_cse_shared_struct_let();
    test_vectorized_array_index();
    test_nested_struct();
    test_nested_struct_cse_shared_inner();
    test_non_dense_stride();
    test_inlined_default_schedule();
    test_parallel_schedule();
    test_runtime_index_into_inlined_pack();
    printf("Success!\n");
    return 0;
}
