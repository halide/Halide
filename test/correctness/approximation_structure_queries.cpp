#include "Halide.h"
#include <cstdio>
#include <string>

using namespace Halide;

// Structure queries on a round trip: the unit behind a stage
// (Approximation::target()), the call that defines a Func
// (ApproximationResult::defining_call()), and a component's Funcs as stored
// and as decoded (stored_components(), decoded_components()), and the struct
// field a layout's port is stored in (StructLayout::port_field()).

namespace {

using Tags = ApproximationTags;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

constexpr int B = 8, K = 64;

// Symmetric quantization of (within, block): codes in [-7, 7] and a scale
// per block.
struct Quantizer {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var i("i"), b("b");
        RDom r(0, B);
        Func amax("q_amax"), scale("q_scale"), codes("q_codes");
        amax(b) = maximum(abs(in[0](r, b)));
        scale(b) = amax(b) / 7.0f;
        codes(i, b) = cast<int8_t>(clamp(round(in[0](i, b) / select(scale(b) == 0.0f, 1.0f, scale(b))), -7.0f, 7.0f));
        return {codes, scale};
    }

    std::vector<Func> decode(const std::vector<Func> &encoded) const {
        Var i("i"), b("b");
        Func values("q_values");
        values(i, b) = cast<float>(encoded[0](i, b)) * encoded[1](b);
        return {values};
    }

    ApproximationSignature signature() const {
        return {{{"blocks", Float(32), 2}},
                {{"codes", Int(8), 2, ApproximationRange(-7, 7), Tags{"q"}},
                 {"scale", Float(32), 1, std::nullopt, Tags{"step"}}}};
    }
};

// Codes stored as indices into a table of `entries` values, decoded by a
// lookup: the table is an intermediate Func of the decode.
struct Codebook {
    int entries;

    Func encode(Func in) const {
        Var i("i"), b("b");
        Func index("cb_index");
        index(i, b) = cast<uint8_t>(in(i, b) + 8);
        return index;
    }

    Func decode(Func in) const {
        Var i("i"), b("b"), v("v");
        Func table("cb_table"), values("cb_values");
        table(v) = cast<int8_t>(v - 8);
        values(i, b) = table(clamp(cast<int>(in(i, b)), 0, entries - 1));
        return values;
    }
};

Pointwise f16() {
    return Pointwise{"f16",
                     [](Expr x) { return cast<float16_t>(x); },
                     [](Expr x) { return cast<float>(x); }};
}

bool has(const std::vector<Func> &fs, const Func &f) {
    for (const Func &g : fs) {
        if (g.name() == f.name()) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> names(const std::vector<Func> &fs) {
    std::vector<std::string> result;
    for (const Func &f : fs) {
        result.push_back(f.name());
    }
    return result;
}

int test_target() {
    Approximation cb = Codebook{16}, quant = Quantizer{}, none;
    CHECK(cb.target<Codebook>() && cb.target<Codebook>()->entries == 16);
    CHECK(!cb.target<Quantizer>() && quant.target<Quantizer>() && !none.target<Codebook>());
    // The unit as passed in, and core units alike.
    Approximation c = Compose(BlockReshape{B}, quant);
    CHECK(c.target<Compose>() && c.target<Compose>()->stages.size() == 2 && !c.target<Parallel>());
    CHECK(Approximation(Identity{}).target<Identity>());
    return 0;
}

// A record of the codes' indices and the scale.
int test_record() {
    Approximation quant = Quantizer{}, cb = Codebook{16}, half = f16(), identity = Identity{};
    Type record = Type::Struct({{"codes", UInt(8), B}, {"scale", Float(16)}});
    Approximation layout = StructLayout(record, {"codes", "scale"});
    Approximation scheme = Compose(BlockReshape{B}, quant, Parallel{{"codes", cb}, {"scale", half}}, layout, identity);
    Var k("k");
    Func w("w"), g("g");
    w(k) = sin(cast<float>(k) * 0.37f) * 3.0f;
    g(k) = w(k) * 2.0f;
    ApproximationResult r = w.approximate_by(scheme, {g});

    // The table and the values read from it are the codebook's.
    Func values = r.decoded_by(cb);
    const ApproximationTraceNode *call = r.defining_call(values);
    CHECK(call && call->stage.same_as(cb) && call->stage.target<Codebook>()->entries == 16);
    CHECK(call->intermediates.size() == 1 && call->intermediates[0].name().rfind("cb_table", 0) == 0);
    CHECK(r.defining_call(call->intermediates[0]) == call);
    // A layout's field is its decode's; a Func of no call has none.
    Func field = r.decoded_by(layout, "codes");
    CHECK(r.defining_call(field) && r.defining_call(field)->stage.same_as(layout));
    // ... and the layout tells the struct field each of its ports is stored
    // in, the field's port by its place among the call's.
    const ApproximationTraceNode *fields = r.defining_call(field);
    const StructLayout *sl = fields->stage.target<StructLayout>();
    CHECK(sl && fields->ports.size() == 2 && fields->ports[0].name() == field.name());
    CHECK(sl->port_field(0).type == UInt(8) && sl->port_field(0).array_extent == B);
    CHECK(sl->port_field(1).type == Float(16) && !sl->port_field(1).array_extent);
    CHECK(!r.defining_call(g) && !r.defining_call(Func()));
    // Identity passes the record through, both ways: the layout's encode
    // defines it.
    CHECK(r.encoded.size() == 1 && r.defining_call(r.encoded[0]) && r.defining_call(r.encoded[0])->stage.same_as(layout));
    // The encode side's Funcs, by the encode trace.
    CHECK(r.defining_call(r.encoded_by(cb)) && r.defining_call(r.encoded_by(cb))->stage.same_as(cb));

    // The codes are stored as the field of indices and decoded to the
    // codebook's values; the scale as the float16 field, decoded to float.
    CHECK(names(r.stored_components("q")) == names({field}));
    CHECK(names(r.decoded_components("q")) == names({values}));
    CHECK(names(r.stored_components("step")) == names({r.decoded_by(layout, "scale")}));
    CHECK(names(r.decoded_components("step")) == names({r.decoded_by(half)}));
    CHECK(r.stored_components("step")[0].type() == Float(16) && r.decoded_components("step")[0].type() == Float(32));
    CHECK(r.stored_components("unknown").empty() && r.decoded_components("unknown").empty());

    // Unlike decode_funcs(tag), the components do not depend on the
    // schedule: still there once computed at root.
    values.compute_root();
    CHECK(!has(r.decode_funcs("q"), values) && names(r.decoded_components("q")) == names({values}));

    Buffer<float> out = g.realize({K}), ref = Func(w).realize({K});
    for (int i = 0; i < K; i++) {
        CHECK(std::abs(out(i) / 2.0f - ref(i)) <= 3.0f / 7.0f / 2.0f + 0.01f);
    }
    return 0;
}

// Planar codes, their indices stored widened: no decode call splits the
// encoded codes out of others, so they are stored as they are; the indices'
// decode keeps their tags.
int test_planar() {
    Approximation quant = Quantizer{}, cb = Codebook{16};
    Approximation widen = Pointwise{"widen", [](Expr x) { return cast<uint16_t>(x); }, [](Expr x) { return cast<uint8_t>(x); }};
    Approximation scheme = Compose(BlockReshape{B}, quant, Parallel{{"codes", Compose(cb, widen)}});
    Var k("k");
    Func w("w"), g("g");
    w(k) = cos(cast<float>(k) * 0.21f);
    g(k) = w(k);
    ApproximationResult r = w.approximate_by(scheme, {g});
    CHECK(r.encoded.size() == 2);
    CHECK(names(r.stored_components("q")) == names({r.encoded[0]}));
    CHECK(r.encoded[0].type() == UInt(16) && names(r.decoded_components("q")) == names({r.decoded_by(cb)}));
    CHECK(names(r.stored_components("step")) == names({r.encoded[1]}));
    CHECK(names(r.decoded_components("step")) == names({r.encoded[1]}));
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (test_target() || test_record() || test_planar()) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
