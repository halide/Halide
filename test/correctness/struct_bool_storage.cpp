#include "Halide.h"
#include <cstdio>

using namespace Halide;

// Struct-typed buffers whose fields are Bool exercise the "value type differs
// from storage type" paths in the backends. Bool is uint1, but memory is
// byte-addressable, so a bool load/store must convert numerically (cast);
// bit-reinterpreting uint1 <-> uint8 is rejected outright by
// Reinterpret::make (mismatched bit widths), so any backend that takes that
// path fails to compile the pipeline rather than producing wrong numbers.
//
// struct_mixed_field_stores.cpp covers the same store-per-field shape with
// float16/uint32 fields (whose storage upgrade happens to be a legal
// same-width reinterpret); gpu_struct_type.cpp covers bool fields on device
// backends. This is the host-side counterpart for the CodeGen_LLVM bool
// conversions below, which every CodeGen_LLVM-derived backend shares (the CPU
// backends and CodeGen_PTX_Dev): measured over the whole host correctness
// suite, those two conversions run hundreds of times but never once with a
// bool type, because uint1-typed loads and stores only reach codegen for
// struct fields.
namespace {

Type bool_block_type() {
    return Type::Struct({{"flags", Bool(), 8}});
}

void run_bool_field_variant(const char *name, bool vectorize) {
    const int num_blocks = 8;
    Var i("i"), blk("blk");

    Func producer("bool_producer");
    std::vector<Expr> vals;
    for (int k = 0; k < 8; k++) {
        vals.push_back((blk + k) % 3 == 0);
    }
    producer(blk) = pack_struct(bool_block_type(), vals);

    Func result("bool_result");
    result(i, blk) = select(field(producer(blk), "flags")[i], 17, -9);

    // compute_root keeps the struct-typed buffer a real allocation (an inlined
    // producer would be folded into a literal pack_struct() and never touch
    // memory at all).
    producer.compute_root();
    result.compute_root();
    if (vectorize) {
        result.vectorize(i, 4);
    }

    Buffer<int> out = result.realize({8, num_blocks});
    out.copy_to_host();
    for (int b = 0; b < num_blocks; b++) {
        for (int k = 0; k < 8; k++) {
            int expected = (b + k) % 3 == 0 ? 17 : -9;
            if (out(k, b) != expected) {
                printf("struct_bool_storage (%s): result(%d, %d) = %d instead of %d\n",
                       name, k, b, out(k, b), expected);
                exit(1);
            }
        }
    }
}

// Bool fields interleaved with a wider field: the bool field must keep its own
// byte-per-element storage at its own offset, without the neighbouring
// Int(32) field's storage width leaking into the index arithmetic.
void run_mixed_fields_variant() {
    Type t = Type::Struct({{"flags", Bool(), 4}, {"score", Int(32)}});
    Var i("i"), blk("blk");

    Func producer("mixed_producer");
    std::vector<Expr> mixed_vals = {blk % 2 == 0, blk % 3 == 0, blk % 5 == 0, blk % 7 == 0,
                                   cast<int32_t>(blk * 100)};
    producer(blk) = pack_struct(t, mixed_vals);

    Func result("mixed_result");
    result(i, blk) =
        select(field(producer(blk), "flags")[i], 1, 0) + field(producer(blk), "score");

    producer.compute_root();
    result.compute_root();
    result.vectorize(i, 2);

    Buffer<int> out = result.realize({4, 8});
    out.copy_to_host();
    for (int b = 0; b < 8; b++) {
        bool expected_flags[4] = {b % 2 == 0, b % 3 == 0, b % 5 == 0, b % 7 == 0};
        for (int k = 0; k < 4; k++) {
            int expected = (expected_flags[k] ? 1 : 0) + b * 100;
            if (out(k, b) != expected) {
                printf("struct_bool_storage (mixed): result(%d, %d) = %d instead of %d\n",
                       k, b, out(k, b), expected);
                exit(1);
            }
        }
    }
}

}  // namespace

int main() {
    run_bool_field_variant("scalar", false);
    run_bool_field_variant("vectorized", true);
    run_mixed_fields_variant();

    printf("Success!\n");
    return 0;
}
