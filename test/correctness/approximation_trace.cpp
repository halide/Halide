#include "Halide.h"
#include <cstdio>
#include <sstream>

using namespace Halide;

namespace {

struct Named {
    Func encode(const Func &f) const {
        return f;
    }
    Func decode(const Func &f) const {
        return f;
    }
    std::string name() const {
        return "MyName";
    }
};

// A user unit that runs two handles inside its own encode()/decode().
struct Pair {
    Approximation first, second;

    std::vector<Func> encode(const std::vector<Func> &inputs) const {
        return second.encode(first.encode(inputs).encoded).encoded;
    }
    std::vector<Func> decode(const std::vector<Func> &encoded) const {
        return first.decode(second.decode(encoded).decoded).decoded;
    }
    std::string name() const {
        return "Pair";
    }
};

// Drop "$N" uniquifier suffixes so the expected text is stable.
std::string normalize(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '$') {
            while (i + 1 < s.size() && isdigit((unsigned char)s[i + 1])) {
                i++;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

std::vector<std::string> names(const std::vector<Func> &fs) {
    std::vector<std::string> result;
    for (const Func &f : fs) {
        result.push_back(f.name());
    }
    return result;
}

}  // namespace

int main() {
    // Labels.
    {
        Approximation comp = Identity{};
        CHECK(comp.label() == "Identity");
        CHECK(Approximation(BlockReshape{32}).label() == "BlockReshape");
        CHECK(Approximation(Compose{Identity{}, Identity{}}).label() == "Compose");
        CHECK(Approximation(Apply{2, Identity{}}).label() == "Apply[2]");
        CHECK(Approximation(StorageCast<float, int8_t>{}).label() == "StorageCast<float, signed char>");
        CHECK(Approximation().label().empty());

        Approximation named = Named{};
        CHECK(named.label() == "MyName");

        Approximation copy = named;
        Approximation renamed = named.labelled("custom");
        CHECK(renamed.same_as(named));
        CHECK(named.label() == "custom");  // shared state
        CHECK(copy.label() == "custom");
        CHECK(renamed.label() == "custom");

        Approximation ctor(Named{}, "explicit");
        CHECK(ctor.label() == "explicit");
        CHECK(!ctor.same_as(named));
    }

    // Tree shape, printing, stage_ports.
    Func src("src");
    Var x("x");
    src(x) = x;
    Func consumer("consumer");
    consumer(x) = src(x) + 1;

    Approximation neg = Pointwise{"neg", [](Expr e) { return -e; }, [](Expr e) { return -e; }};
    Approximation inc = Pointwise{"inc", [](Expr e) { return e + 1; }, [](Expr e) { return e - 1; }};
    Approximation dbl(Pointwise{"dbl", [](Expr e) { return e * 2; }, [](Expr e) { return e / 2; }}, "Double");
    Approximation pair = Pair{inc, dbl};
    Approximation scheme = Compose{Apply{0, pair}, neg};

    ApproximationResult r = src.approximate_by(scheme, {consumer});

    const ApproximationTraceNode &et = r.encode_trace;
    CHECK(et.stage.same_as(scheme));
    CHECK(et.label == "Compose");
    // Compose encodes back-to-front: neg first, then Apply[0].
    CHECK(et.children.size() == 2);
    CHECK(et.children[0].stage.same_as(neg));
    CHECK(et.children[0].children.empty());
    CHECK(et.children[1].label == "Apply[0]");
    CHECK(et.children[1].children.size() == 1);
    const ApproximationTraceNode &ep = et.children[1].children[0];
    CHECK(ep.stage.same_as(pair));
    CHECK(ep.children.size() == 2);
    CHECK(ep.children[0].stage.same_as(inc));
    CHECK(ep.children[1].stage.same_as(dbl));
    CHECK(ep.children[1].label == "Double");

    // Decode runs front-to-back, and Pair undoes its stages in reverse.
    const ApproximationTraceNode &dt = r.decode_trace;
    CHECK(dt.stage.same_as(scheme));
    CHECK(dt.children.size() == 2);
    CHECK(dt.children[0].label == "Apply[0]");
    CHECK(dt.children[1].stage.same_as(neg));
    const ApproximationTraceNode &dp = dt.children[0].children[0];
    CHECK(dp.stage.same_as(pair));
    CHECK(dp.children.size() == 2);
    CHECK(dp.children[0].stage.same_as(dbl));
    CHECK(dp.children[1].stage.same_as(inc));

    // The flat view is the post-order flattening of the tree, and the
    // lookups still work.
    CHECK(r.encoded_stage_outputs.size() == 6);
    CHECK(r.encoded_stage_outputs[0].stage.same_as(neg));
    CHECK(r.encoded_stage_outputs[5].stage.same_as(scheme));
    CHECK(r.encoded_by(inc).name() == "inc_encode");
    CHECK(r.decoded_by(dbl).name() == "dbl_decode");
    CHECK(r.decoded_by(neg).name() == r.replacement.name());

    std::ostringstream trace;
    trace << et;
    const char *expected_trace =
        "Compose -> dbl_encode\n"
        "  intermediates: neg_encode, inc_encode\n"
        "  Pointwise -> neg_encode\n"
        "  Apply[0] -> dbl_encode\n"
        "    intermediates: inc_encode\n"
        "    Pair -> dbl_encode\n"
        "      intermediates: inc_encode\n"
        "      Pointwise -> inc_encode\n"
        "      Double -> dbl_encode\n";
    if (normalize(trace.str()) != expected_trace) {
        printf("Unexpected trace:\n%s", trace.str().c_str());
        return 1;
    }

    std::ostringstream full;
    full << r;
    const char *expected_full =
        "encode:\n"
        "  Compose -> dbl_encode\n"
        "    intermediates: neg_encode, inc_encode\n"
        "    Pointwise -> neg_encode\n"
        "    Apply[0] -> dbl_encode\n"
        "      intermediates: inc_encode\n"
        "      Pair -> dbl_encode\n"
        "        intermediates: inc_encode\n"
        "        Pointwise -> inc_encode\n"
        "        Double -> dbl_encode\n"
        "decode:\n"
        "  Compose -> neg_decode\n"
        "    intermediates: dbl_decode, inc_decode\n"
        "    Apply[0] -> inc_decode\n"
        "      intermediates: dbl_decode\n"
        "      Pair -> inc_decode\n"
        "        intermediates: dbl_decode\n"
        "        Double -> dbl_decode\n"
        "        Pointwise -> inc_decode\n"
        "    Pointwise -> neg_decode\n";
    if (normalize(full.str()) != expected_full) {
        printf("Unexpected result printout:\n%s", full.str().c_str());
        return 1;
    }

    // Trace order, encode side first; the replacement (neg_decode) is excluded.
    const std::vector<std::string> expected_ports = {
        "neg_encode", "inc_encode", "dbl_encode", "dbl_decode", "inc_decode"};
    std::vector<std::string> got = names(r.stage_ports());
    for (std::string &n : got) {
        n = normalize(n);
    }
    CHECK(got == expected_ports);
    CHECK(r.is_stage_port(r.encoded_by(inc)));
    CHECK(!r.is_stage_port(r.replacement));
    CHECK(!r.is_stage_port(consumer));

    printf("Success!\n");
    return 0;
}
