#include "Halide.h"
#include "halide_approximation_testing.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>

using namespace Halide;
using namespace Halide::ApproximationTesting;

namespace {

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

constexpr int QB = 32;

std::vector<Var> make_vars(int n, const std::string &prefix) {
    std::vector<Var> vs;
    for (int i = 0; i < n; i++) {
        vs.emplace_back(prefix + std::to_string(i));
    }
    return vs;
}

std::vector<Expr> with_first(Expr first, const std::vector<Var> &rest) {
    std::vector<Expr> args = {std::move(first)};
    args.insert(args.end(), rest.begin(), rest.end());
    return args;
}

// A per-block absmax int8 quantizer on (within, block, rest...) values: codes
// in [-127, 127] and one float scale per block.
struct BlockInt8 {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        std::vector<Var> vs = make_vars(in[0].dimensions() - 1, "bq");
        Var kk("kk");
        RDom r(0, QB, "amax_r");
        Func amax("bi8_amax"), scale("bi8_scale"), codes("bi8_codes");
        amax(vs) = 0.0f;
        amax(vs) = max(amax(vs), abs(in[0](with_first(r, vs))));
        scale(vs) = amax(vs) / 127.0f;
        codes(with_first(kk, vs)) =
            cast<int8_t>(round(in[0](with_first(kk, vs)) * select(scale(vs) == 0.0f, 0.0f, 1.0f / scale(vs))));
        return {codes, scale};
    }

    std::vector<Func> decode(const std::vector<Func> &encoded) const {
        std::vector<Var> vs = make_vars(encoded[1].dimensions(), "bq");
        Var kk("kk");
        Func out("bi8_decoded");
        out(with_first(kk, vs)) = cast<float>(encoded[0](with_first(kk, vs))) * cast<float>(encoded[1](vs));
        return {out};
    }

    ApproximationSignature signature() const {
        return {{{"block", Float(32)}}, {{"codes", Int(8), std::nullopt, ApproximationRange(-127, 127)}, {"scale", Float(32)}}};
    }
};

// Half an fp16 ulp of x (or of the smallest subnormal).
Expr half_ulp16(Expr x) {
    return max(abs(cast<double>(x)) * Expr(std::ldexp(1.0, -11)), Expr(std::ldexp(1.0, -25)));
}

// Round to fp16, with half an fp16 ulp as the declared bound. The rounding
// is strict: with fast floating-point math, a consumer that widens the
// result again in registers (like the block sum below) may see the value
// before rounding.
Approximation f16() {
    return Pointwise{"f16", [](Expr x) { return strict_float(cast<float16_t>(x)); }, [](Expr x) { return cast<float>(x); }}
        .with_types(Float(32), Float(16))
        .with_error_bound(half_ulp16);
}

// The same rounding, kept in float32.
Approximation f16_in_f32() {
    return Pointwise{"f16_in_f32", [](Expr x) { return strict_float(cast<float>(cast<float16_t>(x))); }, [](Expr x) { return x; }}
        .with_types(Float(32), Float(32))
        .with_error_bound(half_ulp16);
}

// The block sum of the values, scale * sum(codes), from the codes and scale.
Func block_sum(const std::vector<Func> &from) {
    const Func &codes = from[0], &scale = from[1];
    std::vector<Var> vs = make_vars(scale.dimensions(), "bs");
    RDom r(0, QB, "bs_r");
    Func total("block_code_total"), sum("block_sum");
    total(vs) = 0.0f;
    total(vs) += cast<float>(codes(with_first(r, vs)));
    sum(vs) = cast<float>(scale(vs)) * total(vs);
    return sum;
}

Approximation derive_sum() {
    return Derive{"sum", {"codes", "scale"}, block_sum};
}

// The scale is rounded to fp16, then the sum is derived from the rounded
// scale and rounded to fp16 in turn: the stored sum is within half an ulp of
// the block sum of the decoded values.
Approximation consistent_scheme(Approximation *quant = nullptr) {
    Approximation q = BlockInt8{};
    if (quant) {
        *quant = q;
    }
    return Compose{BlockReshape{QB}, q, Parallel{{"scale", f16()}}, derive_sum(), Parallel{{"sum", f16()}}};
}

// The sum derived from the unrounded scale: rounding the scale afterwards
// leaves the stored sum with no declared relation to the decoded values.
Approximation unrounded_scheme() {
    return Compose{BlockReshape{QB}, BlockInt8{}, derive_sum(), Parallel{{"scale", f16()}, {"sum", f16()}}};
}

// The sum stored exactly, in float32.
Approximation exact_scheme() {
    return Compose{BlockReshape{QB}, BlockInt8{}, Parallel{{"scale", f16()}}, derive_sum()};
}

// Two lossy stages after Derive.
Approximation twice_rounded_scheme() {
    return Compose{BlockReshape{QB}, BlockInt8{}, Parallel{{"scale", f16()}}, derive_sum(),
                   Parallel{{"sum", Compose{f16_in_f32(), f16()}}}};
}

// A stage that claims to be lossless but changes the stored sum.
Approximation off_by_one_scheme() {
    Approximation bump = Pointwise{"bump", [](Expr x) { return x + 1.0f; }, [](Expr x) { return x; }}
                             .with_types(Float(32), Float(32))
                             .with_lossless();
    return Compose{BlockReshape{QB}, BlockInt8{}, Parallel{{"scale", f16()}}, derive_sum(), Parallel{{"sum", bump}}};
}

template<typename F>
bool fails_with(F &&fn, const std::string &needle) {
    try {
        fn();
    } catch (const CompileError &e) {
        if (std::string(e.what()).find(needle) == std::string::npos) {
            printf("unexpected error: %s\n", e.what());
            return false;
        }
        return true;
    }
    return false;
}

struct Setup {
    Buffer<float> xb;
    Func x, consumer;
    Approximation quant;
    ApproximationResult result;
    DerivedPort port;
    Func dq;  // the quantizer's decoded values, (within, block, rest...)
};

Setup set_up(const Approximation &scheme, const Approximation &quant, std::vector<int> extents) {
    Setup s;
    s.xb = generate(Distribution::normal(0, 1), Float(32), extents, 7);
    s.x = Func("x");
    s.consumer = Func("x_consumer");
    std::vector<Var> vs = make_vars((int)extents.size(), "x");
    std::vector<Expr> args(vs.begin(), vs.end());
    s.x(vs) = s.xb(args);
    s.consumer(vs) = s.x(vs);
    s.quant = quant;
    s.result = s.x.approximate_by(scheme, {s.consumer});
    s.port = s.result.derived_port("sum");
    if (quant.defined()) {
        s.dq = s.result.decoded_by(quant);
    }
    return s;
}

int test_introspection() {
    Approximation quant;
    Approximation scheme = consistent_scheme(&quant);
    Setup s = set_up(scheme, quant, {QB * 8});
    CHECK(s.result.derived_ports().size() == 1);
    const DerivedPort &p = s.port;
    CHECK(p.name == "sum");
    CHECK(p.stage.as_derive() != nullptr && p.stage.as_derive()->port() == "sum");
    CHECK((p.from_names == std::vector<std::string>{"codes", "scale"}));
    CHECK(p.from.size() == 2 && p.from[0].type() == Int(8) && p.from[1].type() == Float(16));
    CHECK(p.stored.type() == Float(32) && p.stored.dimensions() == 1);
    CHECK(p.definition.dimensions() == 1);
    CHECK(!p.exact && p.bound_problem.empty() && p.error_bound.defined());
    CHECK(p.lossy_stages.size() == 1);
    // The stored port is the decode-side Func that Derive's decode received.
    CHECK(s.result.encoded_by(p.stage, "sum").defined());

    // Without the lossy stage, the port is exact.
    Setup e = set_up(exact_scheme(), Approximation(), {QB * 8});
    CHECK(e.port.exact && e.port.bound_problem.empty() && e.port.lossy_stages.empty());

    // Bounds do not compose.
    std::vector<DerivedPort> ur;
    {
        Buffer<float> xb = generate(Distribution::normal(0, 1), Float(32), {QB * 4}, 3);
        Func x("x2"), y("y2");
        Var i("i");
        x(i) = xb(i);
        y(i) = x(i);
        ur = x.approximate_by(unrounded_scheme(), {y}).derived_ports();
    }
    CHECK(ur.size() == 1 && ur[0].bound_problem.find("do not compose") != std::string::npos);
    {
        Buffer<float> xb = generate(Distribution::normal(0, 1), Float(32), {QB * 4}, 3);
        Func x("x3"), y("y3");
        Var i("i");
        x(i) = xb(i);
        y(i) = x(i);
        ur = x.approximate_by(twice_rounded_scheme(), {y}).derived_ports();
    }
    CHECK(ur.size() == 1 && ur[0].bound_problem.find("more than one lossy stage") != std::string::npos);
    return 0;
}

// An affine weight (scale * code + min, per block) dotted with the
// quantized values: the min times the block sum of the values is outlined as
// its own Func, then replaced by the stored sum.
int test_substitution() {
    const int blocks = 8;
    Approximation quant;
    Setup s = set_up(consistent_scheme(&quant), quant, {QB * blocks});

    Buffer<int8_t> qw = generate(Distribution::uniform_int(0, 15), Int(8), {QB, blocks}, 11);
    Buffer<float> dw = generate(Distribution::uniform(0.01f, 0.1f), Float(32), {blocks}, 12);
    Buffer<float> mw = generate(Distribution::uniform(-1, 1), Float(32), {blocks}, 13);

    Var b("b");
    RDom r(0, QB, "dot_r");
    Func act_sum("act_sum"), dot_codes("dot_codes"), dot("dot");
    act_sum(b) = 0.0f;
    act_sum(b) += s.dq(r, b);
    dot_codes(b) = 0.0f;
    dot_codes(b) += cast<float>(qw(r, b)) * s.dq(r, b);
    dot(b) = dw(b) * dot_codes(b) + mw(b) * act_sum(b);

    Buffer<float> before = dot.realize({blocks});
    Buffer<float> term = act_sum.realize({blocks});

    DerivedSubstitution sub = act_sum.approximate_by(s.port, {dot});
    CHECK(sub.replacement.defined() && !sub.exact && sub.error_bound.defined());
    CHECK(sub.port_args.size() == 1 && Internal::equal(sub.port_args[0], b));

    Buffer<float> after = dot.realize({blocks});
    Buffer<float> stored = sub.replacement.realize({blocks});
    Buffer<double> bound = sub.error_bound.realize({blocks});
    int differs = 0;
    for (int i = 0; i < blocks; i++) {
        // The block sum of the decoded values is exact in float32 here, so
        // the stored sum is within the declared bound of it.
        CHECK(std::fabs((double)stored(i) - term(i)) <= bound(i));
        CHECK(std::fabs((double)after(i) - before(i)) <= std::fabs(mw(i)) * bound(i) + 1e-5 * (1 + std::fabs(before(i))));
        differs += stored(i) != term(i);
    }
    printf("substitution: the stored sum differs from the computed one in %d of %d blocks\n", differs, blocks);

    // The same term written on the ports directly, with the scale outside the
    // sum, matches too.
    Func direct("direct_sum");
    direct(b) = cast<float>(s.port.from[1](b)) * sum(cast<float>(s.port.from[0](r, b)));
    DerivedPortMatch m = match_derived_port(direct, s.port);
    CHECK(m.matched && m.port_args.size() == 1 && Internal::equal(m.port_args[0], b));

    // Integer casts that are exact on the codes' range are the identity.
    Func recast("recast_sum");
    recast(b) = cast<float>(s.port.from[1](b)) *
                sum(cast<float>(cast<int8_t>(cast<int32_t>(s.port.from[0](r, b)) + 0)));
    CHECK(match_derived_port(recast, s.port).matched);

    // likely() and promise_clamped() tags on indices are not operations.
    Func tagged("tagged_sum");
    tagged(b) = cast<float>(s.port.from[1](b)) *
                sum(cast<float>(s.port.from[0](unsafe_promise_clamped(likely(r), 0, QB - 1), b)));
    CHECK(match_derived_port(tagged, s.port).matched);
    return 0;
}

// The arguments come from unifying the term's calls with the definition's.
int test_argument_mapping() {
    const int blocks = 4, rows = 3;
    Approximation quant;
    Setup s = set_up(consistent_scheme(&quant), quant, {QB * blocks, rows});
    CHECK(s.port.stored.dimensions() == 2);

    Var row("row"), b("b");
    RDom r(0, QB, "map_r");
    Func shifted("shifted_sum"), consumer("shifted_consumer");
    shifted(row, b) = 0.0f;
    shifted(row, b) += s.dq(r, b + 1, row);
    consumer(row, b) = shifted(row, b) * 2.0f;
    Buffer<float> term = shifted.realize({rows, blocks - 1});

    DerivedSubstitution sub = shifted.approximate_by(s.port, {consumer});
    CHECK(sub.port_args.size() == 2);
    CHECK(Internal::equal(sub.port_args[0], b + 1));
    CHECK(Internal::equal(sub.port_args[1], row));

    Buffer<float> stored = sub.replacement.realize({rows, blocks - 1});
    Buffer<double> bound = sub.error_bound.realize({rows, blocks - 1});
    Buffer<float> out = consumer.realize({rows, blocks - 1});
    for (int y = 0; y < rows; y++) {
        for (int i = 0; i < blocks - 1; i++) {
            CHECK(std::fabs((double)stored(y, i) - term(y, i)) <= bound(y, i));
            CHECK(out(y, i) == stored(y, i) * 2.0f);
        }
    }
    return 0;
}

// A schedule substitutes a derived port after Pipeline::sever() has cut the
// encoded ports (e.g. quantized weights supplied as inputs). The port's
// stored and `from` Funcs must be decode-side Funcs that read the severed
// inputs, whatever follows Derive: here Identity, which decodes to the
// encoded Funcs themselves, or nothing.
int test_after_sever() {
    const int blocks = 6;
    for (bool identity : {true, false}) {
        Approximation quant = BlockInt8{};
        Approximation scheme = identity ? Approximation(Compose{BlockReshape{QB}, quant, derive_sum(), Identity{}}) :
                                          Approximation(Compose{BlockReshape{QB}, quant, derive_sum()});
        ImageParam xv(Float(32), 1, "sever_x");
        Func x("sever_x_values"), use("sever_use"), dot("sever_dot"), act_sum("sever_act_sum");
        Var i("i"), b("b");
        RDom r(0, QB, "sever_r");
        x(i) = xv(i);
        use(i) = x(i);
        ApproximationResult res = x.approximate_by(scheme, {use});
        Func dq = res.decoded_by(quant);
        act_sum(b) = 0.0f;
        act_sum(b) += dq(r, b);
        dot(b) = act_sum(b) + dq(1, b);  // reads the codes and scale too
        DerivedPort port = res.derived_port("sum");
        for (const Func &e : res.encoded) {
            CHECK(port.stored.name() != e.name());
            for (const Func &f : port.from) {
                CHECK(f.name() != e.name());
            }
        }

        // The offline half: the encoded ports of x.
        CHECK(res.encoded.size() == 3);
        Buffer<float> xb = generate(Distribution::normal(0, 1), Float(32), {QB * blocks}, 5);
        xv.set(xb);
        Buffer<int8_t> codes = res.encoded[0].realize({QB, blocks});
        Buffer<float> scale = res.encoded[1].realize({blocks});
        Buffer<float> sum = res.encoded[2].realize({blocks});

        ImageParam codes_in(Int(8), 2, "sever_codes"), scale_in(Float(32), 1, "sever_scale"),
            sum_in(Float(32), 1, "sever_sum");
        Pipeline({dot, port.stored}).sever(res.encoded, {codes_in, scale_in, sum_in});
        DerivedPortMatch m = match_derived_port(act_sum, port);
        if (!m) {
            printf("%s: %s\n  term %s\n  definition %s\n", identity ? "Identity" : "Derive last", m.reason.c_str(),
                   m.term_form.c_str(), m.definition_form.c_str());
        }
        CHECK(m.matched);
        act_sum.approximate_by(port, {dot});

        std::set<std::string> args;
        for (const Argument &a : Pipeline(dot).infer_arguments()) {
            args.insert(a.name);
        }
        CHECK((args == std::set<std::string>{"sever_codes", "sever_scale", "sever_sum"}));
        codes_in.set(codes);
        scale_in.set(scale);
        sum_in.set(sum);
        xv.set(Buffer<float>(QB * blocks).fill(0.0f));  // unread
        Buffer<float> out = dot.realize({blocks});
        for (int k = 0; k < blocks; k++) {
            CHECK(out(k) == sum(k) + (float)codes(1, k) * scale(k));
        }
    }
    return 0;
}

int test_rejections() {
    if (!Halide::exceptions_enabled()) {
        printf("[SKIP] rejections need exceptions\n");
        return 0;
    }
    const int blocks = 4;
    Approximation quant;
    Setup s = set_up(consistent_scheme(&quant), quant, {QB * blocks});
    Var b("b");
    Func consumer("rej_consumer");

    auto rejects = [&](Func term, const std::string &needle) {
        consumer = Func("rej_consumer");
        consumer(b) = term(b) + 1.0f;
        return fails_with([&]() { term.approximate_by(s.port, {consumer}); }, needle);
    };
    const std::string mismatch = "does not equal derived port";

    // A sum over half the block.
    RDom half(0, QB / 2, "half_r");
    Func half_sum("half_sum");
    half_sum(b) = 0.0f;
    half_sum(b) += s.dq(half, b);
    CHECK(rejects(half_sum, mismatch));

    // The codes summed with another port's scale (a weight scale), and the
    // other Func's codes with the right scale.
    RDom r(0, QB, "rej_r");
    Buffer<float> dw = generate(Distribution::uniform(0.01f, 0.1f), Float(32), {blocks}, 12);
    Buffer<int8_t> qw = generate(Distribution::uniform_int(-8, 7), Int(8), {QB, blocks}, 11);
    Func wrong_scale("wrong_scale");
    wrong_scale(b) = dw(b) * sum(cast<float>(s.port.from[0](r, b)));
    CHECK(rejects(wrong_scale, mismatch));
    Func wrong_codes("wrong_codes");
    wrong_codes(b) = cast<float>(s.port.from[1](b)) * sum(cast<float>(qw(r, b)));
    CHECK(rejects(wrong_codes, mismatch));

    // The codes of another block.
    Func other_block("other_block");
    other_block(b) = cast<float>(s.port.from[1](b)) * sum(cast<float>(s.port.from[0](r, b + 1)));
    CHECK(rejects(other_block, mismatch));

    // A rounding in between is never the identity, even in a real-valued
    // match: here each decoded value is rounded to fp16 before it is summed.
    Func rounded("rounded_sum");
    rounded(b) = 0.0f;
    rounded(b) += cast<float>(cast<float16_t>(s.dq(r, b)));
    CHECK(rejects(rounded, mismatch));

    // The sum of the original values: what a Stored-like substitution would
    // claim, but nothing relates it to the decoded ports.
    Func original("original_sum");
    original(b) = sum(s.x(r + b * QB));
    CHECK(rejects(original, mismatch));

    // The stored port is not its own definition.
    Func stored_itself("stored_itself");
    stored_itself(b) = s.port.stored(b);
    CHECK(rejects(stored_itself, mismatch));

    // A port with no declared bound cannot be substituted.
    {
        Buffer<float> xb = generate(Distribution::normal(0, 1), Float(32), {QB * blocks}, 3);
        Func x("ux"), y("uy");
        Var i("i");
        x(i) = xb(i);
        y(i) = x(i);
        ApproximationResult ur = x.approximate_by(unrounded_scheme(), {y});
        DerivedPort up = ur.derived_port("sum");
        Func term("unrounded_term");
        term(b) = cast<float>(up.from[1](b)) * sum(cast<float>(up.from[0](r, b)));
        Func c("unrounded_consumer");
        c(b) = term(b);
        CHECK(fails_with([&]() { term.approximate_by(up, {c}); }, "declares no bound"));
    }
    return 0;
}

// Integer codes, no scale, on blocks of AB values (k, b).
constexpr int AB = 64, SUB = 16;

struct Int8Codes {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Func q("i8c_codes");
        Var k("k"), b("b");
        q(k, b, _) = cast<int8_t>(clamp(round(in[0](k, b, _)), -127, 127));
        return {q};
    }
    std::vector<Func> decode(const std::vector<Func> &e) const {
        Func v("i8c_decoded");
        Var k("k"), b("b");
        v(k, b, _) = cast<float>(e[0](k, b, _));
        return {v};
    }
    ApproximationSignature signature() const {
        return {{{"block", Float(32)}}, {{"codes", Int(8), std::nullopt, ApproximationRange(-127, 127)}}};
    }
};

// A port whose definition indexes the codes by an affine expression of one
// of its Vars: index(t, r, b) for sub-block t, its value r, block b.
struct AffinePort {
    Approximation codes = Int8Codes{};
    ApproximationResult res;
    DerivedPort port;
    Func dq;

    AffinePort(Func x, Func use, std::function<Expr(Expr, Expr, Expr)> index, Type stored = Int(16)) {
        Derive::Definition sums = [=](const std::vector<Func> &from) {
            Func s("affine_sums");
            Var t("t"), b("b");
            RDom r(0, SUB, "affine_r");
            s(t, b) = cast(stored, sum(cast<int>(from[0](index(t, r, b), b))));
            return s;
        };
        res = x.approximate_by(Compose{BlockReshape{AB}, codes, Derive{"sums", {"codes"}, sums}}, {use});
        port = res.derived_port("sums");
        dq = res.decoded_by(codes);
    }
};

// A definition argument c * v + w (c a constant, w free of v, binders and
// other Vars allowed) determines v from the term's argument e: (e - w) / c,
// where that divides exactly. The candidate is only a proposal: the
// definition at it must still normalize to the term.
int test_affine_arguments() {
    const int blocks = 3;
    Buffer<float> xb(AB * blocks);
    for (int i = 0; i < AB * blocks; i++) {
        xb(i) = (float)((i * 37) % 255 - 127);
    }
    Var i("i"), t("t"), b("b");
    RDom r(0, SUB, "term_r");
    auto setup = [&](const char *name, std::function<Expr(Expr, Expr, Expr)> index, Type stored = Int(16)) {
        Func x(std::string(name) + "_x"), use(std::string(name) + "_use");
        x(i) = xb(i);
        use(i) = x(i);
        return AffinePort(x, use, std::move(index), stored);
    };
    auto report = [](const DerivedPortMatch &m) {
        if (!m) {
            printf("  %s\n  term %s\n  definition %s\n", m.reason.c_str(), m.term_form.c_str(), m.definition_form.c_str());
        }
    };

    // Sums of each 16 codes (q8_K's bsums): codes(16 t + r, b), read through
    // the port, also at the next sub-block.
    {
        AffinePort a = setup("sub", [](Expr t, Expr r, Expr b) { return t * SUB + r; });
        Func term("sub_term");
        term(t, b) = 0.0f;
        term(t, b) += a.dq(t * SUB + r, b);
        DerivedPortMatch m = match_derived_port(term, a.port);
        report(m);
        CHECK(m.matched && m.port_args.size() == 2);
        CHECK(Internal::equal(m.port_args[0], t) && Internal::equal(m.port_args[1], b));

        Func next("sub_next");
        next(t, b) = 0.0f;
        next(t, b) += a.dq(t * SUB + SUB + r, b);
        m = match_derived_port(next, a.port);
        report(m);
        CHECK(m.matched && Internal::equal(Internal::simplify(m.port_args[0] - (t + 1)), 0));
    }

    // Read through the port: approximate_by needs a stored type that widens
    // exactly to the term's, so this port stores floats.
    {
        AffinePort a = setup("read", [](Expr t, Expr r, Expr b) { return t * SUB + r; }, Float(32));
        Func term("read_term"), consumer("read_consumer");
        term(t, b) = 0.0f;
        term(t, b) += a.dq(t * SUB + r, b);
        consumer(t, b) = term(t, b) * 2.0f;
        Buffer<float> before = consumer.realize({AB / SUB, blocks});
        DerivedSubstitution sub = term.approximate_by(a.port, {consumer});
        CHECK(sub.replacement.defined() && sub.port_args.size() == 2);
        Buffer<float> stored = a.res.encoded[1].realize({AB / SUB, blocks});
        Buffer<float> after = consumer.realize({AB / SUB, blocks});
        for (int bb = 0; bb < blocks; bb++) {
            for (int tt = 0; tt < AB / SUB; tt++) {
                CHECK(after(tt, bb) == before(tt, bb) && after(tt, bb) == 2.0f * stored(tt, bb));
            }
        }
    }

    // A negative coefficient: the sub-blocks counted from the block's end.
    {
        AffinePort a = setup("neg", [](Expr t, Expr r, Expr b) { return (AB - 1) - t * SUB - r; });
        Func term("neg_term");
        term(t, b) = 0.0f;
        term(t, b) += a.dq((AB - 1) - t * SUB - r, b);
        DerivedPortMatch m = match_derived_port(term, a.port);
        report(m);
        CHECK(m.matched && Internal::equal(m.port_args[0], t));
    }

    // w with another of the definition's Vars, determined by another
    // argument (b, the block, also the codes' second index).
    {
        AffinePort a = setup("other", [](Expr t, Expr r, Expr b) { return t * SUB + b + r; });
        Func term("other_term");
        term(t, b) = 0.0f;
        term(t, b) += a.dq(t * SUB + b + r, b);
        DerivedPortMatch m = match_derived_port(term, a.port);
        report(m);
        CHECK(m.matched && Internal::equal(m.port_args[0], t) && Internal::equal(m.port_args[1], b));
    }

    // Rejections: a coefficient that does not divide (8 t + r against
    // 16 t + r) proposes nothing; a proposal whose sums differ (8 of the 16
    // values) does not match.
    {
        AffinePort a = setup("rej", [](Expr t, Expr r, Expr b) { return t * SUB + r; });
        Func half("rej_half"), short_sum("rej_short");
        half(t, b) = 0.0f;
        half(t, b) += a.dq(t * (SUB / 2) + r, b);
        DerivedPortMatch m = match_derived_port(half, a.port);
        CHECK(!m.matched && m.reason.find("determines the definition's argument 0") != std::string::npos);
        RDom r8(0, SUB / 2, "short_r");
        short_sum(t, b) = 0.0f;
        short_sum(t, b) += a.dq(t * SUB + r8, b);
        m = match_derived_port(short_sum, a.port);
        CHECK(!m.matched && m.reason.find("differ") != std::string::npos);
    }
    return 0;
}

bool error_occurred = false;
std::string error_message;
void record_error(JITUserContext *, const char *msg) {
    error_occurred = true;
    error_message = msg;
}

// The relation, checked where the port is read, catches a corrupted buffer.
int test_runtime_check() {
    const int blocks = 8;
    Approximation quant;
    Approximation scheme = consistent_scheme(&quant);
    Buffer<float> xb = generate(Distribution::normal(0, 1), Float(32), {QB * blocks}, 5);
    Func x("rt_x");
    Var i("i"), kk("kk"), b("b");
    x(i) = xb(i);
    EncodeResult enc = scheme.encode({x});
    CHECK(enc.encoded.size() == 3 && enc.encoded_ports[2].name == "sum");
    Buffer<int8_t> codes = enc.encoded[0].realize({QB, blocks});
    Buffer<float16_t> scale = enc.encoded[1].realize({blocks});
    Buffer<float16_t> stored = enc.encoded[2].realize({blocks});

    for (bool corrupt : {false, true}) {
        for (DerivedPortCheck check : {DerivedPortCheck::Static, DerivedPortCheck::Runtime}) {
            Buffer<float16_t> sums = stored.copy();
            if (corrupt) {
                sums(3) = float16_t((float)sums(3) + 1.0f);
            }
            Func codes_in("codes_in"), scale_in("scale_in"), sum_in("sum_in");
            codes_in(kk, b) = codes(kk, b);
            scale_in(b) = scale(b);
            sum_in(b) = sums(b);
            DecodeResult dec = scheme.decode({codes_in, scale_in, sum_in});
            std::vector<DerivedPort> ports = derived_ports(enc.trace, dec.trace);
            // from[0] is a decode-side Func of its own that reads codes_in.
            CHECK(ports.size() == 1 && ports[0].from[0].name() != codes_in.name());
            const Internal::Call *read = ports[0].from[0].value().as<Internal::Call>();
            CHECK(read && read->name == codes_in.name());

            Func dq;
            for (const ApproximationStageOutputs &o : dec.stage_outputs) {
                if (o.stage.same_as(quant)) {
                    dq = o.ports[0];
                }
            }
            CHECK(dq.defined());
            RDom r(0, QB, "rt_r");
            Func term("rt_term"), out("rt_out");
            term(b) = 0.0f;
            term(b) += dq(r, b);
            out(b) = term(b) * 3.0f;
            term.approximate_by(ports[0], {out}, check);

            error_occurred = false;
            out.jit_handlers().custom_error = record_error;
            Buffer<float> result(blocks);
            out.realize(result);
            bool expect = corrupt && check == DerivedPortCheck::Runtime;
            CHECK(error_occurred == expect);
            if (expect) {
                printf("runtime check: %s\n", error_message.c_str());
                CHECK(error_message.find("derived port sum") != std::string::npos);
            } else {
                for (int k = 0; k < blocks; k++) {
                    CHECK(result(k) == (float)sums(k) * 3.0f);
                }
            }
        }
    }
    return 0;
}

int test_derived_ports_hold() {
    Distribution normal = Distribution::normal(0, 1);
    PropertyResult good = check_property(consistent_scheme(), derived_ports_hold(), normal, Float(32), {QB * 16, 2}, 4, 1);
    std::cout << good;
    CHECK(good.passed);
    CHECK(check_property(exact_scheme(), derived_ports_hold(), normal, Float(32), {QB * 16}, 4, 1).passed);

    PropertyResult unrounded = check_property(unrounded_scheme(), derived_ports_hold(), normal, Float(32), {QB * 16}, 2, 1);
    std::cout << unrounded;
    CHECK(!unrounded.passed && unrounded.message.find("do not compose") != std::string::npos);

    PropertyResult lying = check_property(off_by_one_scheme(), derived_ports_hold(), normal, Float(32), {QB * 16}, 2, 1);
    std::cout << lying;
    CHECK(!lying.passed && lying.message.find("declared exact") != std::string::npos);

    Approximation no_derive = Compose{BlockReshape{QB}, BlockInt8{}};
    CHECK(!check_property(no_derive, derived_ports_hold(), normal, Float(32), {QB * 4}, 1, 1).passed);

    // Requantizing is idempotent only if the stored sum is a function of the
    // decoded values.
    PropertyResult idem = check_property(consistent_scheme(), idempotent_requantize(), normal, Float(32), {QB * 64}, 8, 1);
    std::cout << idem;
    CHECK(idem.passed);
    PropertyResult idem_unrounded = check_property(unrounded_scheme(), idempotent_requantize(), normal, Float(32), {QB * 64}, 8, 1);
    std::cout << idem_unrounded;
    CHECK(!idem_unrounded.passed);
    return 0;
}

// A unit whose decode returns what its encode was given: the "stored"
// backdoor, which no unit signature rules out.
struct Peek {
    std::shared_ptr<Func> seen = std::make_shared<Func>();
    std::vector<Func> encode(const std::vector<Func> &in) const {
        *seen = in[0];
        Var k("k");
        Func out("peek_encoded");
        out(k) = cast<float16_t>(in[0](k));
        return {out};
    }
    std::vector<Func> decode(const std::vector<Func> &) const {
        Var k("k");
        Func out("peek_decoded");
        out(k) = (*seen)(k);
        return {out};
    }
};

// A unit that reads a Buffer it captured.
struct Table {
    Buffer<float> table;
    std::vector<Func> encode(const std::vector<Func> &in) const {
        return {in[0]};
    }
    std::vector<Func> decode(const std::vector<Func> &in) const {
        Var k("k");
        Func out("table_decoded");
        out(k) = in[0](k) + table(Expr(0));
        return {out};
    }
};

// A unit whose decode ignores its input.
struct Forget {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        return {in[0]};
    }
    std::vector<Func> decode(const std::vector<Func> &) const {
        Var k("k");
        Func out("forget_decoded");
        out(k) = 0.0f;
        return {out};
    }
};

int test_closed() {
    Distribution normal = Distribution::normal(0, 1);
    PropertyResult good = check_property(consistent_scheme(), closed(), normal, Float(32), {QB * 4}, 1, 1);
    std::cout << good;
    CHECK(good.passed);
    CHECK(check_property(unrounded_scheme(), closed(), normal, Float(32), {QB * 4}, 1, 1).passed);

    Approximation peek = Peek{};
    PropertyResult leaky = check_property(peek, closed(), normal, Float(32), {64}, 1, 1);
    std::cout << leaky;
    CHECK(!leaky.passed);
    CHECK(leaky.message.find("(decode): output") != std::string::npos);
    CHECK(leaky.message.find("a Func of the round trip that is not among the stage's inputs") != std::string::npos);
    CHECK(leaky.message.find(peek.label()) != std::string::npos);

    Buffer<float> t(1);
    t(0) = 0.0f;
    PropertyResult table = check_property(Approximation(Table{t}), closed(), normal, Float(32), {64}, 1, 1);
    std::cout << table;
    CHECK(!table.passed && table.message.find("reads the image") != std::string::npos);

    PropertyResult forget = check_property(Approximation(Forget{}), closed(), normal, Float(32), {64}, 1, 1);
    std::cout << forget;
    CHECK(!forget.passed && forget.message.find("depends on none of the stage's inputs") != std::string::npos);
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    struct Test {
        const char *name;
        int (*fn)();
    };
    for (const Test &t : std::vector<Test>{{"introspection", test_introspection},
                                           {"substitution", test_substitution},
                                           {"argument_mapping", test_argument_mapping},
                                           {"affine_arguments", test_affine_arguments},
                                           {"after_sever", test_after_sever},
                                           {"rejections", test_rejections},
                                           {"runtime_check", test_runtime_check},
                                           {"derived_ports_hold", test_derived_ports_hold},
                                           {"closed", test_closed}}) {
        printf("%s\n", t.name);
        if (t.fn() != 0) {
            printf("%s failed\n", t.name);
            return 1;
        }
    }
    printf("Success!\n");
    return 0;
}
