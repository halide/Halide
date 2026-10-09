#pragma once

// Layouts: the last, lossless stage of a format, from its logical fields
// (scalars per record, or arrays with a leading element dimension) to the
// bytes in memory. Domain-free: nothing here knows what the fields mean.
#include "Halide.h"

namespace ggml {

using namespace Halide;

// Records of `rows` consecutive rows (the dimension after the record one) as
// one: each field becomes an array of rows x its elements, stored in pieces
// of `chunk` bytes (or the whole field, if smaller) taken from the rows in
// turn. (b, n, rest...) records <-> (b, n / rows, rest...), n % rows == 0.
// GGML's repacked q4_0_4x8 is rows 4, chunk 8 (repack.cpp make_block_q4_0x4).
struct Interleave {
    int rows, chunk;
    std::vector<StructField> fields;   // the record's, in memory order
    std::vector<std::string> logical;  // the port order

    // A field's elements and its piece, in elements.
    std::pair<int, int> shape(int slot) const {
        const StructField &f = field(slot);
        int e = f.array_extent.value_or(1), c = std::clamp(chunk * 8 / f.type.bits(), 1, e);
        if (e % c) throw std::invalid_argument("Interleave: chunk does not divide field " + f.name);
        return {e, c};
    }
    const StructField &field(int slot) const {
        return *std::find_if(fields.begin(), fields.end(), [&](const StructField &f) { return f.name == logical[slot]; });
    }
    std::vector<Func> encode(const std::vector<Func> &in) const {
        std::vector<Func> out;
        for (int i = 0; i < (int)in.size(); i++) {
            auto [e, c] = shape(i);
            Var x, b, g;
            Expr p = x / c, el = p / rows * c + x % c, n = g * rows + p % rows;
            Func f(in[i].name() + "_interleaved");
            f(x, b, g, _) = field(i).array_extent ? in[i](el, b, n, _) : in[i](b, n, _);
            out.push_back(f);
        }
        return out;
    }
    std::vector<Func> decode(const std::vector<Func> &enc) const {
        std::vector<Func> out;
        for (int i = 0; i < (int)enc.size(); i++) {
            auto [e, c] = shape(i);
            Var x, b, n;
            Func f(enc[i].name() + "_rows");
            Expr at = (x / c * rows + n % rows) * c + x % c;
            if (field(i).array_extent) {
                f(x, b, n, _) = enc[i](at, b, n / rows, _);
            } else {
                f(b, n, _) = enc[i](n % rows, b, n / rows, _);
            }
            out.push_back(f);
        }
        return out;
    }
    ApproximationSignature signature(const ApproximationPorts &in) const {
        ApproximationSignature s{in, in};
        for (int i = 0; i < (int)in.size() && i < (int)logical.size(); i++) {
            if (in[i].dimensions && !field(i).array_extent) s.outputs[i].dimensions = *in[i].dimensions + 1;
        }
        return s;
    }
    bool lossless() const {
        return true;
    }
    // The record of the interleaved layout.
    Type record() const {
        std::vector<StructField> fs = fields;
        for (StructField &f : fs) {
            f.array_extent = rows * f.array_extent.value_or(1);
        }
        return Type::Struct(fs);
    }
};

// `fields` (memory order) packed as GGML's structs, fed by ports in the
// `logical` order; "aos": one record per row, "<rows>x<chunk>": Interleave;
// "soa": not packed, each field its own planar array (port).
inline Approximation layout(const std::string &spec, std::vector<StructField> fields, std::vector<std::string> logical) {
    if (spec == "aos") return StructLayout{Type::Struct(fields), logical};
    if (spec == "soa") return Identity{};
    int rows = 0, chunk = 0;
    if (sscanf(spec.c_str(), "%dx%d", &rows, &chunk) != 2) throw std::invalid_argument("unknown layout " + spec);
    Interleave il{rows, chunk, std::move(fields), logical};
    return Compose{il, StructLayout{il.record(), logical}};
}

}  // namespace ggml
