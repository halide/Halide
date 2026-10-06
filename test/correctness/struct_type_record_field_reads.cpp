#include "Halide.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <vector>

using namespace Halide;
using namespace Halide::Internal;

// An expression that reads several fields of the same record of a
// struct-typed buffer, e.g. a per-record scale and one element of a per-record
// array, gets the record load CSE'd into a struct-typed Let. Lowering struct
// types must still read each field straight from the buffer: an array field
// read at an index that varies across a vectorized loop has to stay one dense
// vector load, not become a per-lane select over every element of the array.

namespace {

std::vector<uint8_t> make_records(int num_records, int record_bytes) {
    std::vector<uint8_t> data((size_t)num_records * record_bytes);
    uint32_t state = 12345;
    for (uint8_t &byte : data) {
        state = state * 1664525u + 1013904223u;
        byte = (uint8_t)(state >> 24);
    }
    return data;
}

template<typename T>
T read_at(const std::vector<uint8_t> &data, size_t offset) {
    T value;
    memcpy(&value, &data[offset], sizeof(T));
    return value;
}

template<typename T>
void write_at(std::vector<uint8_t> &data, size_t offset, T value) {
    memcpy(&data[offset], &value, sizeof(T));
}

Buffer<> wrap_records(Type record, std::vector<uint8_t> &data, int num_records) {
    halide_dimension_t shape[1] = {{0, num_records, 1, 0}};
    return Buffer<>(record, data.data(), 1, shape);
}

// Check the lowered code of a vectorized pipeline: every vector load from
// `input` must be a dense load (a ramp of stride 1 or -1), there must be at
// least `min_dense_loads` of them, and there must be no per-lane select (and
// no shuffle at all, unless `allow_shuffles`).
void check_dense_loads(const char *name, Func f, const ImageParam &input, int min_dense_loads,
                       bool allow_shuffles = true) {
    Module module = f.compile_to_module({input}, name, get_jit_target_from_environment().with_feature(Target::NoAsserts));
    int dense_loads = 0;
    for (const LoweredFunc &lowered_func : module.functions()) {
        visit_with(
            lowered_func.body,
            [&](auto *self, const Load *op) {
                if (op->name == input.name() && op->type.is_vector()) {
                    const Ramp *ramp = op->index.as<Ramp>();
                    std::optional<int64_t> stride;
                    if (ramp) {
                        stride = as_const_int(ramp->stride);
                    }
                    if (!stride || (*stride != 1 && *stride != -1)) {
                        std::cout << name << ": vector load is not dense: " << Expr(op) << "\n";
                        exit(1);
                    }
                    dense_loads++;
                }
                self->visit_base(op);
            },
            [&](auto *self, const Shuffle *op) {
                if (!allow_shuffles) {
                    std::cout << name << ": unexpected shuffle: " << Expr(op) << "\n";
                    exit(1);
                }
                self->visit_base(op);
            },
            [&](auto *self, const Select *op) {
                if (op->type.is_vector() && !op->condition.type().is_scalar() && !op->condition.as<Broadcast>()) {
                    std::cout << name << ": per-lane select: " << Expr(op) << "\n";
                    exit(1);
                }
                self->visit_base(op);
            });
    }
    if (dense_loads < min_dense_loads) {
        printf("%s: found %d dense vector loads, expected at least %d\n", name, dense_loads, min_dense_loads);
        exit(1);
    }
}

template<typename T>
void check_equal(const char *name, const Buffer<T> &actual, const std::function<T(int, int)> &expected) {
    actual.for_each_element([&](const int *pos) {
        int x = pos[0], y = actual.dimensions() > 1 ? pos[1] : 0;
        T e = expected(x, y);
        if (actual(pos) != e) {
            std::cout << name << ": result(" << x << ", " << y << ") = " << actual(pos)
                      << " instead of " << e << "\n";
            exit(1);
        }
    });
}

// A scale and an array element of one record, flattened over the records.
void test_scale_and_array_element() {
    const int num_records = 9, n = 32;
    Type record = Type::Struct({{"s", Float(32)}, {"q", Int(8), n}});
    const int bytes = 4 + n;
    _halide_user_assert(record.bytes() == bytes);
    std::vector<uint8_t> data = make_records(num_records, bytes);
    for (int r = 0; r < num_records; r++) {
        write_at<float>(data, (size_t)r * bytes, 0.25f * (r - 4));
    }
    ImageParam in(record, 1, "in");
    in.set(wrap_records(record, data, num_records));

    for (int vector_size : {0, 8, 32}) {
        Var k("k"), b("b"), j("j"), ji("ji");
        Func out("out");
        Expr rec = in(k / n);
        out(k) = cast<float>(field(rec, "q")[k % n]) * field(rec, "s");
        out.output_buffer().dim(0).set_min(0);
        if (vector_size) {
            out.split(k, b, j, n, TailStrategy::RoundUp).split(j, j, ji, vector_size).vectorize(ji);
            check_dense_loads("scale_and_array_element", out, in, 1);
        }
        Buffer<float> result = out.realize({num_records * n});
        check_equal<float>("scale_and_array_element", result, [&](int x, int) {
            size_t base = (size_t)(x / n) * bytes;
            return (float)read_at<int8_t>(data, base + 4 + x % n) * read_at<float>(data, base);
        });
    }
}

// A float16 scale and two array fields of one record.
void test_two_array_fields() {
    const int num_records = 7, n = 16;
    Type record = Type::Struct({{"d", Float(16)}, {"lo", UInt(8), n}, {"hi", UInt(8), n}});
    const int bytes = 2 + 2 * n;
    _halide_user_assert(record.bytes() == bytes);
    std::vector<uint8_t> data = make_records(num_records, bytes);
    for (int r = 0; r < num_records; r++) {
        write_at<uint16_t>(data, (size_t)r * bytes, float16_t(0.5f * (r + 1)).to_bits());
    }
    ImageParam in(record, 1, "in");
    in.set(wrap_records(record, data, num_records));

    for (bool vectorize : {false, true}) {
        Var j("j"), b("b");
        Func out("out");
        Expr rec = in(b);
        out(j, b) = (cast<float>(field(rec, "lo")[j]) - cast<float>(field(rec, "hi")[j])) *
                    cast<float>(field(rec, "d"));
        out.bound(j, 0, n);
        if (vectorize) {
            out.vectorize(j);
            check_dense_loads("two_array_fields", out, in, 2);
        }
        Buffer<float> result = out.realize({n, num_records});
        check_equal<float>("two_array_fields", result, [&](int x, int y) {
            size_t base = (size_t)y * bytes;
            float d = (float)float16_t::make_from_bits(read_at<uint16_t>(data, base));
            return ((float)data[base + 2 + x] - (float)data[base + 2 + n + x]) * d;
        });
    }
}

// An array field that is not naturally aligned within its record, so each
// element is read as bytes and reinterpreted.
void test_packed_array_field() {
    const int num_records = 5, n = 8;
    Type record = Type::Struct({{"tag", UInt(8)}, {"v", Int(16), n}});
    const int bytes = 1 + 2 * n;
    _halide_user_assert(record.bytes() == bytes);
    std::vector<uint8_t> data = make_records(num_records, bytes);
    ImageParam in(record, 1, "in");
    in.set(wrap_records(record, data, num_records));

    for (bool vectorize : {false, true}) {
        Var j("j"), b("b");
        Func out("out");
        Expr rec = in(b);
        out(j, b) = cast<int32_t>(field(rec, "v")[j]) + cast<int32_t>(field(rec, "tag"));
        out.bound(j, 0, n);
        if (vectorize) {
            out.vectorize(j);
            // The bytes of each element are reinterpreted straight from
            // one dense load.
            check_dense_loads("packed_array_field", out, in, 1, false);
        }
        Buffer<int32_t> result = out.realize({n, num_records});
        check_equal<int32_t>("packed_array_field", result, [&](int x, int y) {
            size_t base = (size_t)y * bytes;
            return (int32_t)read_at<int16_t>(data, base + 1 + 2 * x) + (int32_t)data[base];
        });
    }
}

// Two elements of the same array field at different varying indices, plus
// the scale.
void test_two_elements_of_one_array() {
    const int num_records = 6, n = 32;
    Type record = Type::Struct({{"s", Float(32)}, {"q", Int(8), n}});
    const int bytes = 4 + n;
    std::vector<uint8_t> data = make_records(num_records, bytes);
    for (int r = 0; r < num_records; r++) {
        write_at<float>(data, (size_t)r * bytes, 1.5f + r);
    }
    ImageParam in(record, 1, "in");
    in.set(wrap_records(record, data, num_records));

    for (bool vectorize : {false, true}) {
        Var j("j"), b("b");
        Func out("out");
        Expr rec = in(b);
        out(j, b) = cast<float>(cast<int16_t>(field(rec, "q")[j]) - cast<int16_t>(field(rec, "q")[n - 1 - j])) *
                    field(rec, "s");
        out.bound(j, 0, n);
        if (vectorize) {
            out.vectorize(j);
            check_dense_loads("two_elements_of_one_array", out, in, 2);
        }
        Buffer<float> result = out.realize({n, num_records});
        check_equal<float>("two_elements_of_one_array", result, [&](int x, int y) {
            size_t base = (size_t)y * bytes;
            int lhs = (int8_t)data[base + 4 + x], rhs = (int8_t)data[base + 4 + n - 1 - x];
            return (float)(lhs - rhs) * read_at<float>(data, base);
        });
    }
}

// The shared struct-typed value is a select between two records.
void test_select_between_records() {
    const int num_records = 8, n = 16;
    Type record = Type::Struct({{"s", Float(32)}, {"q", UInt(8), n}});
    const int bytes = 4 + n;
    std::vector<uint8_t> data = make_records(num_records, bytes);
    for (int r = 0; r < num_records; r++) {
        write_at<float>(data, (size_t)r * bytes, 2.0f - r);
    }
    ImageParam in(record, 1, "in");
    in.set(wrap_records(record, data, num_records));

    for (bool vectorize : {false, true}) {
        Var j("j"), b("b");
        Func out("out");
        Expr rec = select(b % 2 == 0, in(b), in(num_records - 1 - b));
        out(j, b) = cast<float>(field(rec, "q")[j]) * field(rec, "s");
        out.bound(j, 0, n);
        if (vectorize) {
            out.vectorize(j);
            check_dense_loads("select_between_records", out, in, 1);
        }
        Buffer<float> result = out.realize({n, num_records});
        check_equal<float>("select_between_records", result, [&](int x, int y) {
            size_t base = (size_t)(y % 2 == 0 ? y : num_records - 1 - y) * bytes;
            return (float)data[base + 4 + x] * read_at<float>(data, base);
        });
    }
}

// An update that swaps two fields of a record in place. The record must be
// read in full before any of its fields is overwritten, so this Let must not
// be substituted into the per-field stores.
void test_swap_fields_in_place() {
    const int n = 8;
    Type record = Type::Struct({{"a", Int(32)}, {"b", Int(32)}});
    Var x("x");
    Func f("f");
    f(x) = pack_struct(record, {x, 100 + x});
    Expr rec = f(x);
    f(x) = pack_struct(record, {field(rec, "b"), field(rec, "a")});
    Buffer<> result = f.realize({n});
    const uint8_t *data = (const uint8_t *)result.raw_buffer()->host;
    for (int i = 0; i < n; i++) {
        int32_t a, b;
        memcpy(&a, data + 8 * i, 4);
        memcpy(&b, data + 8 * i + 4, 4);
        if (a != 100 + i || b != i) {
            printf("swap_fields_in_place: f(%d) = {%d, %d} instead of {%d, %d}\n", i, a, b, 100 + i, i);
            exit(1);
        }
    }
}

}  // namespace

int main(int argc, char **argv) {
    test_scale_and_array_element();
    test_two_array_fields();
    test_packed_array_field();
    test_two_elements_of_one_array();
    test_select_between_records();
    test_swap_fields_in_place();
    printf("Success!\n");
    return 0;
}
