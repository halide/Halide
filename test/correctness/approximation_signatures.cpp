#include "Halide.h"
#include <cstdio>
#include <memory>
#include <sstream>

using namespace Halide;

namespace {

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

// Declares its ports statically. values -> (codes, scale), one scale per value.
struct Producer {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var x("x");
        Func codes("codes"), scale("scale");
        scale(x) = 0.5f;
        codes(x) = cast<int8_t>(round(in[0](x) / scale(x)));
        return {codes, scale};
    }
    std::vector<Func> decode(const std::vector<Func> &in) const {
        Var x("x");
        Func values("values");
        values(x) = cast<float>(in[0](x)) * in[1](x);
        return {values};
    }
    ApproximationSignature signature() const {
        return {{{"values", Float(32), 1}},
                {{"codes", Int(8), 1}, {"scale", Float(32), 1}}};
    }
};

// Float to float16 bits and back, with no declaration.
struct HalfBits {
    Func encode(const Func &f) const {
        Func g("half_bits");
        g(_) = reinterpret<uint16_t>(cast<float16_t>(f(_)));
        return g;
    }
    Func decode(const Func &f) const {
        Func g("half_value");
        g(_) = cast<float>(reinterpret<float16_t>(f(_)));
        return g;
    }
};

// The same, declared.
struct DeclaredHalfBits : HalfBits {
    ApproximationSignature signature() const {
        return {{{"value", Float(32), 1}}, {{"bits", UInt(16), 1}}};
    }
};

// One input, two outputs, undeclared: the outputs are named positionally.
struct SplitParity {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var x("x");
        Func even("even"), odd("odd");
        even(x) = in[0](2 * x);
        odd(x) = in[0](2 * x + 1);
        return {even, odd};
    }
    std::vector<Func> decode(const std::vector<Func> &in) const {
        Var x("x");
        Func out("merged");
        out(x) = select(x % 2 == 0, in[0](x / 2), in[1](x / 2));
        return {out};
    }
};

// One input, two float outputs, declared: values -> (lo, hi).
struct LoHi {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var x("x");
        Func lo("lo_part"), hi("hi_part");
        lo(x) = in[0](2 * x);
        hi(x) = in[0](2 * x + 1);
        return {lo, hi};
    }
    std::vector<Func> decode(const std::vector<Func> &in) const {
        Var x("x");
        Func out("lohi_merged");
        out(x) = select(x % 2 == 0, in[0](x / 2), in[1](x / 2));
        return {out};
    }
    ApproximationSignature signature() const {
        return {{{"values", Float(32), 1}}, {{"lo", Float(32), 1}, {"hi", Float(32), 1}}};
    }
};

// Records the ports it is handed, and passes everything through.
struct Probe {
    std::shared_ptr<ApproximationPorts> seen = std::make_shared<ApproximationPorts>();

    std::vector<Func> encode(const std::vector<Func> &in, const ApproximationPorts &ports) const {
        *seen = ports;
        return in;
    }
    std::vector<Func> decode(const std::vector<Func> &in, const ApproximationPorts &) const {
        return in;
    }
};

struct DeclaredProbe : Probe {
    ApproximationSignature signature() const {
        return {{{"values"}}, {{"values"}}};
    }
};

std::vector<std::string> names(const ApproximationPorts &ports) {
    std::vector<std::string> result;
    for (const ApproximationPort &p : ports) {
        result.push_back(p.name);
    }
    return result;
}

using Names = std::vector<std::string>;

}  // namespace

int main() {
    // The constructors accept the natural spellings.
    {
        ApproximationPort a{"scale"};
        ApproximationPort b{"scale", Float(32)};
        ApproximationPort c{"scale", Float(32), 1};
        ApproximationPorts ports{{"a"}, {"b", UInt(8)}, {"c", Int(8), 2}};
        CHECK(!a.type && !a.dimensions);
        CHECK(*b.type == Float(32) && !b.dimensions);
        CHECK(*c.dimensions == 1);
        CHECK(names(ports) == Names({"a", "b", "c"}));
    }

    Var x("x");
    Func f("f");
    f(x) = cast<float>(x % 20) * 0.5f;
    Func consumer("consumer");
    consumer(x) = f(x) + 0.0f;

    // A named Apply in a Compose: half-precision scales, found by name.
    Approximation producer = Producer{};
    Approximation half = HalfBits{};
    Approximation apply = Apply("scale", half);
    Approximation scheme = Compose{apply, producer};
    {
        ApproximationResult r = f.approximate_by(scheme, {consumer});
        Buffer<float> out = consumer.realize({64});
        for (int i = 0; i < 64; i++) {
            CHECK(out(i) == (float)(i % 20) * 0.5f);
        }

        CHECK(r.encoded.size() == 2);
        CHECK(names(r.encoded_ports) == Names({"codes", "scale"}));
        CHECK(*r.encoded_ports[0].type == Int(8));
        CHECK(*r.encoded_ports[1].type == UInt(16));
        CHECK(r.encoded[1].types()[0] == UInt(16));

        CHECK(r.encoded_by(producer, "codes").name() == r.encoded[0].name());
        CHECK(r.encoded_by(producer, "scale").types()[0] == Float(32));
        CHECK(r.encoded_by(apply, "scale").name() == r.encoded[1].name());
        CHECK(r.encoded_by(half, "scale").name() == r.encoded[1].name());
        CHECK(r.decoded_by(half, "scale").types()[0] == Float(32));
        CHECK(r.decoded_by(apply, "scale").name() == r.decoded_by(half, "scale").name());
        CHECK(r.decoded_by(apply, "codes").name() == r.encoded[0].name());
        CHECK(r.decoded_by(producer, "values").name() == r.replacement.name());
        // The index forms are unchanged, and a literal 0 is still an index.
        CHECK(r.encoded_by(producer, 1).name() == r.encoded_by(producer, "scale").name());
        CHECK(r.encoded_by(producer, 0).name() == r.encoded_by(producer, "codes").name());
        CHECK(r.encoded_by(producer).name() == r.encoded_by(producer, "codes").name());

        std::ostringstream trace;
        trace << r.encode_trace;
        CHECK(trace.str().find("scale=half_bits") != std::string::npos);
    }

    // Pass-through naming through undeclared single-form units, at any arity.
    {
        Approximation neg = Pointwise{"neg", [](Expr e) { return -e; }, [](Expr e) { return -e; }};
        EncodeResult e = neg.encode({f}, {ApproximationPort("weights")});
        CHECK(names(e.encoded_ports) == Names({"weights"}));
        CHECK(e.trace.port_names == Names({"weights"}));
        DecodeResult d = neg.decode(e.encoded, e.encoded_ports);
        CHECK(names(d.decoded_ports) == Names({"weights"}));

        Approximation chain = Compose{neg, half};
        EncodeResult ce = chain.encode({f}, {ApproximationPort("weights")});
        CHECK(names(ce.encoded_ports) == Names({"weights"}));

        // Without ports, a unit with no declaration names its input positionally.
        CHECK(names(neg.encode({f}).encoded_ports) == Names({"0"}));

        // approximate_by names its input "input", unless the root declares one.
        Probe undeclared;
        Approximation p1 = undeclared;
        (void)f.approximate_by(p1, {});
        CHECK(names(*undeclared.seen) == Names({"input"}));
        DeclaredProbe declared;
        Approximation p2 = declared;
        (void)f.approximate_by(p2, {});
        CHECK(names(*declared.seen) == Names({"values"}));
    }

    // Decode on its own: the declared outputs name the encoded inputs.
    {
        EncodeResult e = scheme.encode({f});
        CHECK(names(e.encoded_ports) == Names({"codes", "scale"}));
        // Sever the ports, as compute_offline does.
        DecodeResult d = scheme.decode(e.encoded);
        CHECK(d.decoded.size() == 1);
        CHECK(names(d.decoded_ports) == Names({"values"}));
        CHECK(d.trace.children.size() == 2);
        CHECK(d.trace.children[0].port_names == Names({"codes", "scale"}));

        DecodeResult pd = producer.decode(producer.encode({f}).encoded);
        CHECK(names(pd.decoded_ports) == Names({"values"}));
    }

    // Resolved signatures.
    Approximation declared_half = DeclaredHalfBits{};
    Approximation declared_scheme = Compose{Apply("scale", declared_half), producer};
    {
        ApproximationSignature s = declared_scheme.signature();
        CHECK(s.known);
        CHECK(names(s.inputs) == Names({"values"}));
        CHECK(*s.inputs[0].type == Float(32) && *s.inputs[0].dimensions == 1);
        CHECK(names(s.outputs) == Names({"codes", "bits"}));
        CHECK(*s.outputs[0].type == Int(8));
        CHECK(*s.outputs[1].type == UInt(16));

        // A static signature's names are authoritative; a contextual one follows its context.
        CHECK(names(producer.signature({ApproximationPort("other")}).inputs) == Names({"values"}));
        ApproximationSignature cast = Approximation(StorageCast<float, uint16_t>{})
                                          .signature({ApproximationPort("w", std::nullopt, 3)});
        CHECK(names(cast.inputs) == Names({"w"}) && names(cast.outputs) == Names({"w"}));
        CHECK(*cast.inputs[0].type == Float(32) && *cast.outputs[0].type == UInt(16));
        CHECK(*cast.outputs[0].dimensions == 3);

        // Undeclared: a single-form unit is pass-through; a multi-form one is unknown.
        ApproximationSignature single = half.signature({ApproximationPort("v")});
        CHECK(single.known && names(single.outputs) == Names({"v"}));
        CHECK(!Approximation(SplitParity{}).signature().known);
        CHECK(!Approximation(Compose{half, SplitParity{}}).signature().known);
    }

    // describe() renders the tree without running anything.
    {
        const char *expected =
            "Compose (values: float32 x1) -> (codes: int8 x1, bits: uint16 x1)\n"
            "  Producer (values: float32 x1) -> (codes: int8 x1, scale: float32 x1)\n"
            "  Apply[scale] (codes: int8 x1, scale: float32 x1) -> (codes: int8 x1, bits: uint16 x1)\n"
            "    DeclaredHalfBits (value: float32 x1) -> (bits: uint16 x1)\n";
        std::string got = declared_scheme.describe();
        if (got != expected) {
            printf("Unexpected describe():\n%s", got.c_str());
            return 1;
        }
        std::ostringstream stream;
        stream << declared_scheme;
        CHECK(stream.str() == expected);

        std::string undeclared = Approximation(Compose{half, SplitParity{}}).describe();
        const char *expected_undeclared =
            "Compose (unknown signature)\n"
            "  SplitParity (unknown signature)\n"
            "  HalfBits (0) -> (0)\n";
        if (undeclared != expected_undeclared) {
            printf("Unexpected describe():\n%s", undeclared.c_str());
            return 1;
        }
    }

    // An undeclared multi-form unit changing arity names its outputs positionally.
    {
        Approximation split = SplitParity{};
        EncodeResult e = split.encode({f}, {ApproximationPort("row")});
        CHECK(names(e.encoded_ports) == Names({"0", "1"}));
        DecodeResult d = split.decode(e.encoded);
        CHECK(names(d.decoded_ports) == Names({"0"}));

        Func even_source("even_source");
        even_source(x) = x;
        ApproximationResult r = even_source.approximate_by(split, {});
        CHECK(r.encoded_by(split, "0").name() == r.encoded[0].name());
        CHECK(r.encoded_by(split, "1").name() == r.encoded[1].name());
        CHECK(r.decoded_by(split, "0").name() == r.replacement.name());
        std::ostringstream trace;
        trace << r;
        CHECK(trace.str().find("0=even") != std::string::npos);
    }

    // Permute restores the pre-permutation names in decode, so a by-name Apply
    // after it works in both directions, with and without an encode context.
    {
        Approximation lohi = LoHi{};
        Approximation lo_half = Apply("lo", half);
        Approximation permute = Permute{{1, 0}};
        Approximation permuted = Compose{permute, lo_half, lohi};

        EncodeResult e = permuted.encode({f});
        CHECK(names(e.encoded_ports) == Names({"hi", "lo"}));
        CHECK(*e.encoded_ports[1].type == UInt(16));

        DecodeResult d = permuted.decode(e.encoded, e.encoded_ports);
        CHECK(names(d.decoded_ports) == Names({"values"}));
        CHECK(d.trace.children.size() == 3);

        // Sever the ports, as compute_offline does.
        DecodeResult sd = permuted.decode(e.encoded);
        CHECK(names(sd.decoded_ports) == Names({"values"}));
        CHECK(sd.decoded[0].types()[0] == Float(32));

        // Each stage sees the names it had on the way in.
        CHECK(sd.trace.children[0].port_names == Names({"lo", "hi"}));
        CHECK(sd.trace.children[1].port_names == Names({"lo", "hi"}));
        CHECK(sd.trace.children[0].input_names == Names({"hi", "lo"}));

        // Permute alone, with and without context.
        EncodeResult pe = permute.encode({f, consumer}, {{"a"}, {"b"}});
        CHECK(names(pe.encoded_ports) == Names({"b", "a"}));
        CHECK(names(permute.decode(pe.encoded, pe.encoded_ports).decoded_ports) == Names({"a", "b"}));
        Permute rotate{{1, 2, 0}};
        EncodeResult re = Approximation(rotate).encode({f, consumer, f}, {{"a"}, {"b"}, {"c"}});
        CHECK(names(re.encoded_ports) == Names({"b", "c", "a"}));
        CHECK(names(Approximation(rotate).decode(re.encoded, re.encoded_ports).decoded_ports) == Names({"a", "b", "c"}));

        // Identity and a positional Apply keep names in decode too.
        Approximation ident = Compose{Identity{}, Apply(1, half), lohi};
        EncodeResult ie = ident.encode({f});
        CHECK(names(ie.encoded_ports) == Names({"lo", "hi"}));
        CHECK(names(ident.decode(ie.encoded, ie.encoded_ports).decoded_ports) == Names({"values"}));
        Approximation by_name_first = Compose{Apply("lo", half), Permute{{1, 0}}, Permute{{1, 0}}, lohi};
        EncodeResult be = by_name_first.encode({f});
        CHECK(names(by_name_first.decode(be.encoded).decoded_ports) == Names({"values"}));
        Approximation two_perms = Compose{Permute{{1, 0}}, Apply("hi", half), Permute{{1, 0}}, lohi};
        EncodeResult tpe = two_perms.encode({f});
        CHECK(names(tpe.encoded_ports) == Names({"lo", "hi"}));
        CHECK(names(two_perms.decode(tpe.encoded, tpe.encoded_ports).decoded_ports) == Names({"values"}));
    }

    printf("Success!\n");
    return 0;
}
