#include "Halide.h"
#include <cstdio>
#include <set>
#include <string>

using namespace Halide;

// Port tags (ApproximationTags): a quantizer declares what its outputs are
// in a vocabulary of the user's own (here "q", "step", "zero"), every other
// unit's ports inherit the tags, and a schedule asks a round trip for the
// Funcs of one component by its tag.

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

// Asymmetric 4-bit quantization of (within, block): codes, a scale and a min
// per block. Only it declares tags.
struct AffineQuantizer {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var i("i"), b("b");
        RDom r(0, B);
        Func lo("aq_lo"), hi("aq_hi"), scale("aq_scale"), codes("aq_codes");
        lo(b) = minimum(in[0](r, b));
        hi(b) = maximum(in[0](r, b));
        scale(b) = (hi(b) - lo(b)) / 15.0f;
        codes(i, b) = cast<uint8_t>(round((in[0](i, b) - lo(b)) / select(scale(b) == 0.0f, 1.0f, scale(b))));
        return {codes, scale, lo};
    }

    std::vector<Func> decode(const std::vector<Func> &encoded) const {
        Var i("i"), b("b");
        Func values("aq_values"), widened("aq_widened"), scaled("aq_scaled");
        widened(i, b) = cast<float>(encoded[0](i, b));
        scaled(i, b) = widened(i, b) * encoded[1](b);
        values(i, b) = scaled(i, b) + encoded[2](b);
        return {values};
    }

    ApproximationSignature signature() const {
        return {{{"blocks", Float(32), 2}},
                {{"codes", UInt(8), 2, ApproximationRange(0, 15), Tags{"q"}},
                 {"scale", Float(32), 1, std::nullopt, Tags{"step"}},
                 {"min", Float(32), 1, std::nullopt, Tags{"zero"}}}};
    }

    std::string name() const {
        return "AffineQuantizer";
    }
};

Pointwise f16() {
    return Pointwise{"f16",
                     [](Expr x) { return cast<float16_t>(x); },
                     [](Expr x) { return cast<float>(x); }};
}

Func code_sum(const std::vector<Func> &from) {
    Var b("b");
    RDom r(0, B);
    Func s("code_sum");
    s(b) = cast<uint16_t>(0);
    s(b) += cast<uint16_t>(from[0](r, b));
    return s;
}

std::set<std::string> names_of(const std::vector<Func> &fs) {
    std::set<std::string> result;
    for (const Func &f : fs) {
        result.insert(f.name());
    }
    return result;
}

bool has(const std::vector<Func> &fs, const std::string &name) {
    return names_of(fs).count(name) > 0;
}

// Tags flow through the signatures: inherited by wire, position, or union.
int test_signatures() {
    Approximation quant = AffineQuantizer{};
    Approximation reshape = BlockReshape{B};
    ApproximationSignature s = Approximation(Compose(reshape, quant)).signature();
    CHECK(s.known && s.inputs.size() == 1 && s.outputs.size() == 3);
    CHECK(s.inputs[0].tags == Tags{});
    CHECK(s.outputs[0].tags == Tags{"q"} && s.outputs[1].tags == Tags{"step"} && s.outputs[2].tags == Tags{"zero"});

    // A Pointwise keeps its input's tags; tags that flow in win.
    ApproximationPort codes("codes", std::nullopt, std::nullopt, std::nullopt, Tags{"q"});
    ApproximationSignature p = Approximation(f16()).signature({codes});
    CHECK(p.inputs[0].tags == Tags{"q"} && p.outputs[0].tags == Tags{"q"});
    CHECK(Approximation(f16()).signature().outputs[0].tags == Tags{});

    // Routed by name, each port keeps its tags through the child or past it.
    ApproximationPorts three = s.outputs;
    ApproximationSignature par = Approximation(Parallel{{"scale", f16()}}).signature(three);
    CHECK(par.outputs.size() == 3 && par.outputs[0].tags == Tags{"q"} && par.outputs[1].tags == Tags{"step"} &&
          par.outputs[2].tags == Tags{"zero"});

    // Permute moves the wires, tags and all.
    ApproximationSignature perm = Approximation(Permute{{2, 0, 1}}).signature(three);
    CHECK(perm.outputs[0].tags == Tags{"zero"} && perm.outputs[1].tags == Tags{"q"} && perm.outputs[2].tags == Tags{"step"});

    // Derive passes its inputs through and gives its port its own tags: none
    // by default (not those of the ports it is derived from).
    ApproximationSignature d = Approximation(Derive("sum", {"codes"}, code_sum)).signature(three);
    CHECK(d.outputs.size() == 4 && d.outputs[0].tags == Tags{"q"} && d.outputs[3].tags == Tags{});
    ApproximationSignature dt = Approximation(Derive("sum", {"codes"}, code_sum, {"total"})).signature(three);
    CHECK(dt.outputs[3].tags == Tags{"total"});

    // A record of fields has the union of their tags.
    Type record = Type::Struct({{"codes", UInt(8), B}, {"scale", Float(16)}});
    ApproximationPorts two{{"codes", UInt(8), 2, std::nullopt, Tags{"q"}}, {"scale", Float(16), 1, std::nullopt, Tags{"step"}}};
    ApproximationSignature rec = Approximation(StructLayout(record, {"codes", "scale"})).signature(two);
    CHECK(rec.outputs.size() == 1 && rec.outputs[0].tags == (Tags{"q", "step"}));
    return 0;
}

// An undeclared multi-Func unit (unknown signature) still passes tags on at
// run time: by position, or the union.
struct Split {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var i("i"), b("b");
        Func hi("split_hi"), lo("split_lo");
        hi(i, b) = in[0](i, b) >> 2;
        lo(i, b) = in[0](i, b) & 3;
        return {hi, lo};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Var i("i"), b("b");
        Func out("split_joined");
        out(i, b) = (e[0](i, b) << 2) | e[1](i, b);
        return {out};
    }
};

int test_undeclared() {
    Var i("i"), b("b");
    Func codes("codes");
    codes(i, b) = cast<uint8_t>(i + b);
    ApproximationPort port("codes", std::nullopt, std::nullopt, std::nullopt, Tags{"q"});
    Approximation split = Split{};
    EncodeResult e = split.encode({codes}, {port});
    CHECK(e.encoded_ports.size() == 2 && e.encoded_ports[0].tags == Tags{"q"} && e.encoded_ports[1].tags == Tags{"q"});
    DecodeResult d = split.decode(e.encoded, {port});
    CHECK(d.decoded_ports[0].tags == Tags{"q"});
    CHECK(d.trace.input_tags == std::vector<Tags>({{"q"}, {"q"}}));
    return 0;
}

Approximation make_scheme(Approximation &quant, Approximation &derive, Approximation &scale_f16, Approximation &min_f16,
                          Approximation &layout) {
    quant = AffineQuantizer{};
    derive = Derive("sum", {"codes"}, code_sum, {"total"});
    scale_f16 = f16();
    min_f16 = f16();
    Type record = Type::Struct({{"codes", UInt(8), B}, {"scale", Float(16)}, {"min", Float(16)}, {"sum", UInt(16)}});
    layout = StructLayout(record, {"codes", "scale", "min", "sum"});
    return Compose(BlockReshape{B}, quant, derive, Parallel{{"scale", scale_f16}, {"min", min_f16}}, layout);
}

// A round trip's Funcs by tag.
int test_round_trip() {
    Approximation quant, derive, scale_f16, min_f16, layout;
    Approximation scheme = make_scheme(quant, derive, scale_f16, min_f16, layout);
    Var k("k");
    Func w("w"), g("g");
    w(k) = sin(cast<float>(k) * 0.37f) * 3.0f;
    g(k) = w(k) * 2.0f;
    ApproximationResult r = w.approximate_by(scheme, {g});

    const Tags all_four{"q", "step", "zero", "total"};
    CHECK(r.encoded_ports.size() == 1 && r.encoded_ports[0].tags == all_four);
    CHECK(r.tags_of(r.encoded[0]) == all_four);
    CHECK(r.owns(r.replacement) && r.tags_of(r.replacement) == Tags{});
    CHECK(r.owns(w) && r.tags_of(w) == Tags{});
    CHECK(!r.owns(g) && r.tags_of(g) == Tags{});
    CHECK(r.owns(r.encoded[0]) && !r.owns(Func()));

    // Each field's decode has its port's tags, through the Pointwise too.
    CHECK(r.tags_of(r.decoded_by(layout, "codes")) == Tags{"q"});
    CHECK(r.tags_of(r.decoded_by(layout, "scale")) == Tags{"step"});
    CHECK(r.tags_of(r.decoded_by(layout, "min")) == Tags{"zero"});
    CHECK(r.tags_of(r.decoded_by(layout, "sum")) == Tags{"total"});
    CHECK(r.tags_of(r.decoded_by(scale_f16)) == Tags{"step"});
    CHECK(r.tags_of(r.decoded_by(min_f16)) == Tags{"zero"});
    CHECK(r.tags_of(r.derived_port("sum").stored) == Tags{"total"});

    // Inside the quantizer's decode: the codes' part alone is tagged "q", a
    // mix of components (a partly decoded value) nothing.
    std::vector<Func> codes = r.decode_funcs("q"), all = r.decode_funcs();
    CHECK(has(codes, "aq_widened") && !has(codes, "aq_scaled") && !has(codes, "aq_values"));
    CHECK(r.tags_of(r.decoded_by(quant)) == Tags{});
    for (const std::string name : {"aq_scaled", "aq_values"}) {
        for (const Func &f : all) {
            CHECK(f.name() != name || r.tags_of(f) == Tags{});
        }
    }
    CHECK(has(codes, r.decoded_by(layout, "codes").name()));
    CHECK(has(r.decode_funcs("step"), r.decoded_by(scale_f16).name()));
    CHECK(has(r.decode_funcs("zero"), r.decoded_by(min_f16).name()));
    CHECK(!has(r.decode_funcs("step"), r.decoded_by(min_f16).name()));
    CHECK(r.decode_funcs("unknown").empty());
    // Every decode Func here carries at most one tag.
    size_t tagged = 0;
    for (const std::string tag : {"q", "step", "zero", "total"}) {
        tagged += r.decode_funcs(tag).size();
    }
    size_t untagged = 0;
    for (const Func &f : all) {
        untagged += r.tags_of(f).empty();
    }
    CHECK(tagged + untagged == all.size());

    // The encode side: the per-block parameters by the ports they feed.
    CHECK(r.tags_of(r.encoded_by(quant, "scale")) == Tags{"step"});
    CHECK(r.tags_of(r.encoded_by(derive, "sum")) == Tags{"total"});
    CHECK(r.tags_of(r.encoded_by(scale_f16)) == Tags{"step"});
    int checked = 0;
    for (const ApproximationStageOutputs &o : r.encoded_stage_outputs) {
        CHECK(o.port_tags.size() == o.ports.size() && o.intermediate_tags.size() == o.intermediates.size());
        for (size_t i = 0; o.stage.same_as(quant) && i < o.intermediates.size(); i++) {
            // the block's max feeds the codes (through the scale) and the scale
            if (o.intermediates[i].name() == "aq_hi") {
                CHECK(o.intermediate_tags[i] == (Tags{"q", "step"}));
                checked++;
            }
        }
    }
    CHECK(checked == 1);

    // The round trip still computes: within half a step of each value.
    Buffer<float> out = g.realize({K}), ref = Func(w).realize({K});
    for (int i = 0; i < K; i++) {
        CHECK(std::abs(out(i) / 2.0f - ref(i)) <= 6.0f / 15.0f / 2.0f + 0.01f);
    }
    return 0;
}

// A stand-alone decode (e.g. after Pipeline::sever()) has the same tags.
int test_stand_alone_decode() {
    Approximation quant, derive, scale_f16, min_f16, layout;
    Approximation scheme = make_scheme(quant, derive, scale_f16, min_f16, layout);
    ImageParam records(Type::Struct({{"codes", UInt(8), B}, {"scale", Float(16)}, {"min", Float(16)}, {"sum", UInt(16)}}), 1, "records");
    DecodeResult d = scheme.decode({records});
    CHECK(d.decoded_ports.size() == 1 && d.decoded_ports[0].tags == Tags{});
    CHECK(d.trace.input_tags == std::vector<Tags>({{"q", "step", "zero", "total"}}));
    bool found = false;
    for (const ApproximationStageOutputs &o : d.stage_outputs) {
        if (o.stage.same_as(layout)) {
            CHECK(o.port_tags == std::vector<Tags>({{"q"}, {"step"}, {"zero"}, {"total"}}));
            found = true;
        }
    }
    CHECK(found);
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    for (auto test : {test_signatures, test_undeclared, test_round_trip, test_stand_alone_decode}) {
        if (test() != 0) {
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
