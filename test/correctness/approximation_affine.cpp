#include "Halide.h"
#include <cstdio>

using namespace Halide;

// Pointwise::affine: integer codes a * u + b <-> fields u in [0, n), its
// declared map queried back from a stage, and the Expr shape of each form.

namespace {

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            return 1;                                                       \
        }                                                                   \
    } while (0)

using Form = Pointwise::AffineForm;

// Every code round trips through its field, the field being its index.
int check_map(int a, int b, int n, Form form) {
    Pointwise p = Pointwise::affine("map", a, b, n, form);
    CHECK(p.lossless() && p.affine_map());
    const Pointwise::AffineMap &m = *p.affine_map();
    CHECK(m.a == a && m.b == b && m.n == n && m.form == form);
    CHECK(m.offset_term() == (form == Form::Sum ? b : 0));

    ApproximationSignature sig = p.signature({});
    int lo = std::min(b, b + a * (n - 1)), hi = std::max(b, b + a * (n - 1));
    CHECK(sig.inputs[0].type == Int(8) && sig.outputs[0].type == UInt(8));
    CHECK(sig.inputs[0].range->lo == lo && sig.inputs[0].range->hi == hi);
    CHECK(sig.outputs[0].range->lo == 0 && sig.outputs[0].range->hi == n - 1);

    Var u("u");
    Func codes("codes");
    codes(u) = cast<int8_t>(a * u + b);
    Func fields = p.encode(codes), decoded = p.decode(fields);
    Buffer<uint8_t> f = fields.realize({n});
    Buffer<int8_t> c = decoded.realize({n});
    for (int i = 0; i < n; i++) {
        CHECK(f(i) == i && c(i) == a * i + b);
    }

    // The forms' shapes: only Sum's decode has a constant term of its own.
    Expr e = decoded.function().definition().values()[0];
    if (form == Form::Sum && b != 0) {
        const auto *t = a > 0 ? e.as<Internal::Add>() : nullptr;
        const auto *d = a < 0 ? e.as<Internal::Sub>() : nullptr;
        CHECK(t ? Internal::is_const(t->b, b) : d && Internal::is_const(d->a, b));
    } else if (form == Form::Narrow) {
        CHECK(e.as<Internal::Cast>());
    } else if (form == Form::Select) {
        CHECK(e.as<Internal::Select>());
    }
    return 0;
}

int test_forms() {
    // unsigned fields, offsets, steps (positive and negative) in each form
    CHECK(!check_map(1, 0, 16, Form::Sum));
    CHECK(!check_map(1, -1, 3, Form::Sum));
    CHECK(!check_map(16, -32, 4, Form::Sum));
    CHECK(!check_map(2, 1, 16, Form::Sum));
    CHECK(!check_map(-2, 1, 2, Form::Sum));
    CHECK(!check_map(1, -8, 16, Form::Narrow));
    CHECK(!check_map(16, -128, 16, Form::Narrow));
    CHECK(!check_map(2, -1, 2, Form::Select));
    CHECK(!check_map(-2, 1, 2, Form::Select));
    // a hand-written Pointwise declares none
    CHECK(!Pointwise("pw", [](Expr x) { return x; }, [](Expr x) { return x; }).affine_map());
    return 0;
}

// A schedule finds the map of the stage that defines a decode.
int test_query() {
    Approximation offset = Pointwise::affine("offset", 4, -32, 16);
    Var k("k");
    Func w("w"), g("g");
    w(k) = cast<int8_t>((k % 16) * 4 - 32);
    g(k) = w(k);
    ApproximationResult r = w.approximate_by(offset, {g});
    const ApproximationTraceNode *call = r.defining_call(r.decoded_by(offset));
    CHECK(call && call->stage.target<Pointwise>());
    const auto &m = call->stage.target<Pointwise>()->affine_map();
    CHECK(m && m->a == 4 && m->offset_term() == -32);
    Buffer<int8_t> out = g.realize({32});
    for (int i = 0; i < 32; i++) {
        CHECK(out(i) == (i % 16) * 4 - 32);
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (test_forms() || test_query()) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
