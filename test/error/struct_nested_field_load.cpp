#include "Halide.h"
#include <stdio.h>
#include <vector>

using namespace Halide;

// A struct field whose own type is itself Type::Struct (a nested struct
// field) can't be read directly out of a real (buffer-backed) struct-typed
// Load: LowerStructTypes.cpp's lower_field_read_from_load can only
// Reinterpret a field's bytes into a same-sized plain integer, and a
// struct's runtime tag is always plain UInt(8) regardless of its real byte
// size. Populating the buffer manually (rather than via a materialized
// Halide Store) sidesteps the separate, equally-unsupported Store-side
// restriction (see struct_nested_field_store.cpp) so that this specific,
// documented read-side restriction is what actually fires.
int main(int argc, char **argv) {
    Type inner_t = Type::Struct({{"a", Int(32)}, {"b", UInt(8)}});
    Type outer_t = Type::Struct({{"inner", inner_t}, {"c", Float(32)}});
    const int num_blocks = 4;

    std::vector<uint8_t> data((size_t)outer_t.bytes() * num_blocks, 0);
    halide_dimension_t shape[1] = {
        {0, num_blocks, 1, 0},
    };
    Buffer<uint8_t> Wt_buf(data.data(), 1, shape);

    ImageParam Wt(outer_t, 1, "Wt");
    Wt.set(Wt_buf);

    Var i("i");
    Func consumer("consumer");
    Expr inner_val = field(field(Wt(i), "inner"), "a");
    consumer(i) = inner_val;

    Buffer<int32_t> result = consumer.realize({num_blocks});

    printf("Success!\n");
    return 0;
}
