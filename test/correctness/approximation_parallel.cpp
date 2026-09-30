#include "Halide.h"
#include <cstdio>
#include <set>

using namespace Halide;

namespace {

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

using Names = std::vector<std::string>;

Names names(const ApproximationPorts &ports) {
    Names n;
    for (const ApproximationPort &p : ports) {
        n.push_back(p.name);
    }
    return n;
}

// values -> (codes, scale), with a fixed scale of one half.
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

// One signed byte to a low and a high nibble: one port becomes two.
struct Nibbles {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var x("x");
        Func lo("lo_nibble"), hi("hi_nibble");
        lo(x) = in[0](x) & cast<int8_t>(15);
        hi(x) = in[0](x) >> 4;
        return {lo, hi};
    }
    std::vector<Func> decode(const std::vector<Func> &in) const {
        Var x("x");
        Func v("joined");
        v(x) = cast<int8_t>(in[1](x) * 16 + in[0](x));
        return {v};
    }
    ApproximationSignature signature() const {
        return {{{"byte", Int(8), 1}}, {{"lo", Int(8), 1}, {"hi", Int(8), 1}}};
    }
    bool lossless() const {
        return true;
    }
};

Func source() {
    Var x("x");
    Func f("src");
    f(x) = (cast<float>(x) - 20.0f) * 0.5f;
    return f;
}

int check_round_trip(const Approximation &a, const std::vector<Func> &encoded_from_encode,
                     const EncodeResult &e, Func f) {
    for (Func i : e.intermediates) {
        i.compute_root();
    }
    DecodeResult d = a.decode(e.encoded);
    Buffer<float> out = d.decoded[0].realize({40});
    Buffer<float> ref = f.realize({40});
    for (int i = 0; i < 40; i++) {
        CHECK(out(i) == ref(i));
    }
    (void)encoded_from_encode;
    return 0;
}

int test_named() {
    Func f = source();
    Approximation scheme = Compose{Producer{},
                                   Parallel{{"codes", Nibbles{}},
                                            {"scale", StorageCast<float, float16_t>{}}}};
    EncodeResult e = scheme.encode({f});
    CHECK(names(e.encoded_ports) == Names({"lo", "hi", "scale"}));
    CHECK(e.encoded.size() == 3);
    DecodeResult d = scheme.decode(e.encoded);
    CHECK(names(d.decoded_ports) == Names({"values"}));

    // The children of the Parallel see the names that flow in.
    const ApproximationTraceNode &par = e.trace.children[1];
    CHECK(par.input_names == Names({"codes", "scale"}));
    CHECK(par.port_names == Names({"lo", "hi", "scale"}));
    const ApproximationTraceNode &dpar = d.trace.children[0];
    CHECK(dpar.input_names == Names({"lo", "hi", "scale"}));
    CHECK(dpar.port_names == Names({"codes", "scale"}));

    Approximation alone = scheme;
    ApproximationSignature sig = alone.signature({{"values"}});
    CHECK(names(sig.outputs) == Names({"lo", "hi", "scale"}));

    // Ports the Parallel does not mention pass through unchanged.
    Approximation only_codes = Compose{Producer{}, Parallel{{"codes", Nibbles{}}}};
    EncodeResult oe = only_codes.encode({f});
    CHECK(names(oe.encoded_ports) == Names({"lo", "hi", "scale"}));

    // A name that flows in wins over a child's declared one.
    Approximation renamed = Compose{Producer{}, Parallel{{"codes", Identity{}}}};
    CHECK(names(renamed.encode({f}, {{"weights"}}).encoded_ports) == Names({"codes", "scale"}));

    // Values round trip (fp16 represents multiples of one half exactly).
    return check_round_trip(scheme, e.encoded, e, f);
}

int test_positional() {
    Func f = source();
    Approximation scheme = Compose{Producer{}, Parallel{Nibbles{}, StorageCast<float, float16_t>{}}};
    EncodeResult e = scheme.encode({f});
    CHECK(names(e.encoded_ports) == Names({"lo", "hi", "scale"}));
    CHECK(names(scheme.decode(e.encoded).decoded_ports) == Names({"values"}));
    CHECK(check_round_trip(scheme, e.encoded, e, f) == 0);

    // Identity passes one Func through; widths come from the signatures.
    Approximation with_identity = Compose{Producer{}, Parallel{Identity{}, StorageCast<float, float16_t>{}}};
    EncodeResult ie = with_identity.encode({f});
    CHECK(names(ie.encoded_ports) == Names({"codes", "scale"}));
    CHECK(check_round_trip(with_identity, ie.encoded, ie, f) == 0);
    return 0;
}

template<typename F>
bool fails_with(F &&fn, const std::string &needle) {
    try {
        fn();
    } catch (const CompileError &e) {
        return std::string(e.what()).find(needle) != std::string::npos;
    }
    return false;
}

int test_errors() {
    Func f = source(), g = source();
    Approximation missing = Parallel{{"nope", Identity{}}};
    CHECK(fails_with([&] { missing.encode({f, g}, {{"a"}, {"b"}}); }, "nope"));
    Approximation ambiguous = Parallel{{"a", Identity{}}};
    CHECK(fails_with([&] { ambiguous.encode({f, g}, {{"a"}, {"a"}}); }, "a"));
    Approximation twice = Parallel{{"a", Identity{}}, {"a", Identity{}}};
    CHECK(fails_with([&] { twice.encode({f, g}, {{"a"}, {"b"}}); }, "a"));
    Approximation too_few = Parallel{Identity{}, Identity{}};
    CHECK(fails_with([&] { too_few.encode({f}); }, "2"));
    CHECK(fails_with([&] { too_few.encode({f, g, f}); }, "3"));
    return 0;
}

// Q4_0: BlockReshape, quantize, Parallel, StructLayout.
int test_mirror_and_uniqueness() {
    Type block = Type::Struct({{"d", Float(16)}, {"qs", UInt(8), 16}});
    Approximation scheme = Compose{
        BlockReshape{32},
        SymmetricBlockQuantize{32, 8, BlockRoundingMode::TruncateHalfUpWithOffset,
                               BlockScaleAnchor::ExtremeSignedValue},
        Parallel{{"codes", Compose{AdditiveOffset<int8_t, uint8_t>{8}, PlanarFieldPack{4, 16}}},
                 {"scale", StorageCast<float, float16_t>{}}},
        StructLayout{block, {"qs", "d"}}};
    Var k("k");
    Func f("q4_src");
    f(k) = cast<float>((k % 16) - 8);
    EncodeResult e = scheme.encode({f});
    DecodeResult d = scheme.decode(e.encoded);
    CHECK(names(d.decoded_ports) == Names({"values"}));

    // A stand-alone decode agrees with the encode's context on every stage.
    const size_t n = e.trace.children.size();
    CHECK(n == 4 && d.trace.children.size() == n);
    for (size_t i = 0; i < n; i++) {
        const ApproximationTraceNode &en = e.trace.children[i];
        const ApproximationTraceNode &de = d.trace.children[n - 1 - i];
        CHECK(en.input_names == de.port_names);
        CHECK(en.port_names == de.input_names);
        CHECK(std::set<std::string>(de.port_names.begin(), de.port_names.end()).size() == de.port_names.size());
    }
    std::vector<ApproximationStageOutputs> outs = d.stage_outputs;
    for (const ApproximationStageOutputs &o : outs) {
        CHECK(std::set<std::string>(o.port_names.begin(), o.port_names.end()).size() == o.port_names.size());
    }
    // The struct layout maps slots positionally: flowing names survive it.
    CHECK(e.trace.children[3].input_names == Names({"bytes", "scale"}));
    return 0;
}

}  // namespace

int main() {
    if (test_named() || test_positional() || test_errors() || test_mirror_and_uniqueness()) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
