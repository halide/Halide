#include "Halide.h"
#include <cstdio>

using namespace Halide;

namespace {

// Single-form encode/decode.
struct Negate {
    Func encode(const Func &f) const {
        Func g("negated");
        g(_) = -f(_);
        return g;
    }
    Func decode(const Func &f) const {
        return encode(f);
    }
};

// Multi-form: two Funcs out, back into one.
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

// Mixed: multi encode + single decode.
struct AddOne {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        Var x("x");
        Func g("plus_one");
        g(x) = in[0](x) + 1;
        return {g};
    }
    Func decode(const Func &f) const {
        Var x("x");
        Func g("minus_one");
        g(x) = f(x) - 1;
        return g;
    }
};

// Both a Func and a vector overload: the vector one must win.
struct BothOverloads {
    std::vector<Func> encode(const std::vector<Func> &in) const {
        return in;
    }
    Func encode(const Func &f) const {
        return f;
    }
    std::vector<Func> decode(const std::vector<Func> &in) const {
        return in;
    }
    Func decode(const Func &f) const {
        return f;
    }
};

struct EncodeOnly {
    Func encode(const Func &f) const {
        return f;
    }
};

struct BadReturn {
    int encode(const Func &) const {
        return 0;
    }
    Func decode(const Func &f) const {
        return f;
    }
};

struct NonConstEncode {
    Func encode(const Func &f) {
        return f;
    }
    Func decode(const Func &f) const {
        return f;
    }
};

static_assert(std::is_convertible_v<Negate, Approximation>);
static_assert(std::is_convertible_v<SplitParity, Approximation>);
static_assert(std::is_convertible_v<AddOne, Approximation>);
static_assert(std::is_convertible_v<BothOverloads, Approximation>);
static_assert(std::is_convertible_v<Pointwise, Approximation>);
static_assert(std::is_convertible_v<Compose, Approximation>);
// The unit-side full form is gone: results are not a valid unit return type.
struct FullForm {
    EncodeResult encode(const std::vector<Func> &in) const {
        return {in, {}, {}};
    }
    DecodeResult decode(const std::vector<Func> &in) const {
        return {in, {}, {}};
    }
};

// A unit that internally calls two member handles.
struct Pair {
    Approximation a, b;
    std::vector<Func> encode(const std::vector<Func> &in) const {
        return b.encode(a.encode(in).encoded).encoded;
    }
    std::vector<Func> decode(const std::vector<Func> &in) const {
        return a.decode(b.decode(in).decoded).decoded;
    }
};

static_assert(!std::is_convertible_v<FullForm, Approximation>);
static_assert(!std::is_convertible_v<EncodeOnly, Approximation>);
static_assert(!std::is_convertible_v<BadReturn, Approximation>);
static_assert(!std::is_convertible_v<NonConstEncode, Approximation>);
static_assert(!std::is_convertible_v<int, Approximation>);

template<typename F>
int check_round_trip(const char *what, const Approximation &a, F expected, int n = 16) {
    Func f("f");
    Var x("x");
    f(x) = x * 3 + 1;
    Func g("g");
    g(x) = f(x) * 2;
    ApproximationResult r = f.approximate_by(a, {g});
    for (Func h : r.intermediates) {
        h.compute_root();
    }
    r.replacement.compute_root();
    Buffer<int> out = g.realize({n});
    for (int i = 0; i < n; i++) {
        int want = expected(i) * 2;
        if (out(i) != want) {
            printf("%s: g(%d) = %d, expected %d\n", what, i, out(i), want);
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main() {
    auto identity = [](int i) { return i * 3 + 1; };

    Approximation neg = Negate{};
    if (check_round_trip("single", neg, identity)) return 1;

    Approximation parity = SplitParity{};
    if (check_round_trip("multi", parity, identity)) return 1;

    Approximation add = AddOne{};
    if (check_round_trip("mixed", add, identity)) return 1;

    if (check_round_trip("both", BothOverloads{}, identity)) return 1;

    Approximation pw = Pointwise{"scale",
                                 [](Expr v) { return v * 2; },
                                 [](Expr v) { return v / 2; }};
    if (check_round_trip("pointwise", pw, identity)) return 1;

    // Tuple-valued Pointwise: swap the halves of a pair.
    {
        Var x("x");
        Func t("t");
        t(x) = Tuple(x, x * 10);
        Approximation swap = Pointwise{"swap",
                                       [](const std::vector<Expr> &v) { return std::vector<Expr>{v[1], v[0]}; },
                                       [](const std::vector<Expr> &v) { return std::vector<Expr>{v[1], v[0]}; }};
        EncodeResult e = swap.encode({t});
        if (e.encoded[0].name().rfind("swap_encode", 0) != 0) {
            printf("unexpected pointwise name %s\n", e.encoded[0].name().c_str());
            return 1;
        }
        DecodeResult d = swap.decode(e.encoded);
        Realization re = d.decoded[0].realize({4});
        Buffer<int> a = re[0], b = re[1];
        for (int i = 0; i < 4; i++) {
            if (a(i) != i || b(i) != i * 10) {
                printf("tuple pointwise mismatch at %d\n", i);
                return 1;
            }
        }
    }

    // Stage lookup works for simple-form units, including when composed.
    {
        Approximation h_neg = Negate{}, h_parity = SplitParity{}, h_add = AddOne{};
        Compose scheme{h_parity, h_neg, h_add};
        Func f("src");
        Var x("x");
        f(x) = x;
        Func c("consumer");
        c(x) = f(x);
        ApproximationResult r = f.approximate_by(scheme, {c});
        auto starts_with = [](const Func &fn, const char *prefix) {
            return fn.defined() && fn.name().rfind(prefix, 0) == 0;
        };
        if (!starts_with(r.decoded_by(h_neg), "negated") || !starts_with(r.encoded_by(h_add), "plus_one") ||
            !starts_with(r.decoded_by(h_add), "minus_one") || !r.decoded_by(h_parity).defined()) {
            printf("stage lookup returned unexpected Funcs\n");
            return 1;
        }
    }

    // A hand-written unit that calls other handles is traced automatically.
    {
        Approximation inner_neg = Negate{}, inner_add = AddOne{};
        Approximation pair = Pair{inner_neg, inner_add};
        Func f("pair_src");
        Var x("x");
        f(x) = x;
        EncodeResult e = pair.encode({f});
        DecodeResult d = pair.decode(e.encoded);
        auto names = [](const std::vector<ApproximationStageOutputs> &so) {
            std::string s;
            for (const ApproximationStageOutputs &o : so) {
                s += o.ports[0].name() + ",";
            }
            return s;
        };
        if (e.stage_outputs.size() != 3 || !e.stage_outputs[0].stage.same_as(inner_neg) ||
            !e.stage_outputs[1].stage.same_as(inner_add) || !e.stage_outputs[2].stage.same_as(pair) ||
            d.stage_outputs.size() != 3 || !d.stage_outputs[0].stage.same_as(inner_add) ||
            !d.stage_outputs[1].stage.same_as(inner_neg) || !d.stage_outputs[2].stage.same_as(pair)) {
            printf("nested unit trace wrong: enc [%s] dec [%s]\n", names(e.stage_outputs).c_str(),
                   names(d.stage_outputs).c_str());
            return 1;
        }
        // The pair's inter-stage Func is discovered as an intermediate.
        if (e.intermediates.size() != 1 || e.intermediates[0].name().rfind("negated", 0) != 0) {
            printf("nested unit intermediates wrong\n");
            return 1;
        }
        Buffer<int> out = d.decoded[0].realize({8});
        for (int i = 0; i < 8; i++) {
            if (out(i) != i) {
                printf("nested unit round trip wrong at %d\n", i);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
