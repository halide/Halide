#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// A struct field whose own type is itself Type::Struct (a nested struct
// field) can't be materialized into a real buffer: LowerStructTypes.cpp's
// Store splitter can only Reinterpret a field's value into a same-sized
// plain integer, and a struct's runtime tag is always plain UInt(8)
// regardless of its real byte size, so splitting a nested struct field this
// way is rejected with a clear error instead of miscompiling.
int main(int argc, char **argv) {
    Type inner_t = Type::Struct({{"a", Int(32)}, {"b", UInt(8)}});
    Type outer_t = Type::Struct({{"inner", inner_t}, {"c", Float(32)}});
    Var i("i");

    Func producer("producer");
    producer(i) = pack_struct(outer_t, {pack_struct(inner_t, {cast<int32_t>(i * 10), cast<uint8_t>(i)}), cast<float>(i) + 0.5f});
    producer.compute_root();

    Buffer<> result = producer.realize({4});

    printf("Success!\n");
    return 0;
}
