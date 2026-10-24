#include "Halide.h"
#include "test_sharding.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using std::string;
using std::vector;

using namespace Halide;
using namespace Halide::Internal;

// Whether e calls a Func or reads a Param with the given name (or the name
// given to it, if made unique as name$n).
bool mentions(const Expr &e, const string &name) {
    auto matches = [&](const string &n) {
        return n == name || (starts_with(n, name + "$") && name.find('$') == string::npos);
    };
    bool found = false;
    visit_with(
        e,
        [&](auto *self, const Call *op) {
            found |= matches(op->name);
            self->visit_base(op);
        },
        [&](auto *self, const Variable *op) {
            found |= matches(op->name);
        });
    return found;
}

string str(const Expr &e) {
    std::ostringstream o;
    o << e;
    return o.str();
}

bool check(bool ok, const string &what) {
    if (!ok) {
        printf("%s\n", what.c_str());
    }
    return ok;
}

// The body a FuncVec entry accumulates must mention everything in `yes` and
// nothing in `no`.
bool check_mentions(const Expr &e, const vector<string> &yes, const vector<string> &no, const string &what) {
    bool ok = true;
    for (const string &n : yes) {
        ok &= check(mentions(e, n), what + " does not mention " + n + ": " + str(e));
    }
    for (const string &n : no) {
        ok &= check(!mentions(e, n), what + " mentions " + n + ": " + str(e));
    }
    return ok;
}

bool expect_error(const std::function<void()> &fn, const string &expected) {
    if (!Halide::exceptions_enabled()) {
        return true;
    }
    try {
        fn();
    } catch (const Halide::CompileError &e) {
        if (string(e.what()).find(expected) == string::npos) {
            printf("Expected an error containing:\n  %s\ngot:\n%s\n", expected.c_str(), e.what());
            return false;
        }
        return true;
    }
    printf("Expected an error containing:\n  %s\n", expected.c_str());
    return false;
}

constexpr int N = 37;

struct Inputs {
    Buffer<int> qb{N}, pb{N}, cb{N};
    Func q{"q"}, p{"p"}, c{"c"};
    Param<int> d{"d"}, e{"e"}, m{"m"};
    Var x{"x"};
    RDom r{0, N, "r"};

    Inputs() {
        for (int i = 0; i < N; i++) {
            qb(i) = (i * 7) % 11 - 5;
            pb(i) = (i * 5) % 9 - 4;
            cb(i) = (i * 3) % 7 - 3;
        }
        q(x) = qb(x);
        p(x) = pb(x);
        c(x) = cb(x);
        d.set(3);
        e.set(-2);
        m.set(5);
    }

    int sum(const std::function<int(int)> &term) const {
        int s = 0;
        for (int i = 0; i < N; i++) {
            s += term(i);
        }
        return s;
    }
};

// abadams's example from the review of distribute() (halide/Halide#9508):
// f() += (d*q(r) + m) * (e*p(r)) with hoist_invariants({q, p}) becomes
// d*e*sum(q(r)*p(r)) + m*e*sum(p(r)).
int abadams_example_test() {
    Inputs in;
    Func f{"f"};
    f() = 0;
    f() += (in.d * in.q(in.r) + in.m) * (in.e * in.p(in.r));
    FuncVec intms = f.update().hoist_invariants({in.q, in.p});

    bool ok = check(intms.size() == 3, "expected one Func per listed Func plus the residual");
    ok = ok && check(intms[0].defined() && intms[1].defined(), "expected parts for q and p");
    ok = ok && check(!intms[2].defined(), "expected no residual");
    if (!ok) {
        return 1;
    }
    ok &= check_mentions(intms[0].update_value(), {"q", "p"}, {"d", "e", "m"}, "q's part");
    ok &= check_mentions(intms[1].update_value(), {"p"}, {"q", "d", "e", "m"}, "p's part");
    ok &= check_mentions(f.update_value(), {intms[0].name(), intms[1].name(), "d", "e", "m"}, {"q", "p"},
                         "the write-back");
    if (!ok) {
        return 1;
    }

    Buffer<int> out = f.realize();
    Buffer<int> qp = intms[0].realize(), ps = intms[1].realize();
    const int d = 3, e = -2, m = 5;
    const int ref_qp = in.sum([&](int i) { return in.qb(i) * in.pb(i); });
    const int ref_p = in.sum([&](int i) { return in.pb(i); });
    if (!check(qp() == ref_qp && ps() == ref_p, "the parts accumulate the wrong values") ||
        !check(out() == d * e * ref_qp + m * e * ref_p, "abadams example: wrong result")) {
        return 1;
    }
    return 0;
}

// The split depends only on which terms call which Funcs, not on how the sum is
// spelled.
int syntax_invariance_test() {
    using Spelling = std::function<Expr(Inputs &)>;
    vector<Spelling> spellings = {
        [](Inputs &in) { return (in.d * in.q(in.r) + in.m) * (in.e * in.p(in.r)); },
        [](Inputs &in) { return in.d * in.e * in.q(in.r) * in.p(in.r) + in.m * in.e * in.p(in.r); },
        [](Inputs &in) { return in.e * (in.p(in.r) * in.m + in.d * (in.p(in.r) * in.q(in.r))); },
        [](Inputs &in) { return in.m * in.p(in.r) * in.e + (in.q(in.r) * in.e) * (in.p(in.r) * in.d); },
        [](Inputs &in) { return (in.q(in.r) * in.d - (0 - in.m)) * in.p(in.r) * in.e; },
    };
    for (size_t s = 0; s < spellings.size() + 2; s++) {
        Inputs in;
        Func f{"f"};
        f() = 0;
        if (s < spellings.size()) {
            f() += spellings[s](in);
        } else if (s == spellings.size()) {
            // The self-reference need not come first.
            f() = (in.e * in.p(in.r)) * (in.d * in.q(in.r) + in.m) + f();
        } else {
            f() -= (in.e * in.p(in.r)) * (0 - in.d * in.q(in.r) - in.m);
        }
        FuncVec intms = f.update().hoist_invariants({in.q, in.p});
        const string what = "spelling " + std::to_string(s);
        if (!check(intms.size() == 3 && intms[0].defined() && intms[1].defined() && !intms[2].defined(),
                   what + ": expected parts for q and p and no residual") ||
            !check_mentions(intms[0].update_value(), {"q", "p"}, {"d", "e", "m"}, what + ": q's part") ||
            !check_mentions(intms[1].update_value(), {"p"}, {"q", "d", "e", "m"}, what + ": p's part")) {
            return 1;
        }
        Buffer<int> out = f.realize();
        Buffer<int> qp = intms[0].realize(), ps = intms[1].realize();
        const int ref_qp = in.sum([&](int i) { return in.qb(i) * in.pb(i); });
        const int ref_p = in.sum([&](int i) { return in.pb(i); });
        if (!check(qp() == ref_qp && ps() == ref_p, what + ": the parts accumulate the wrong values") ||
            !check(out() == 3 * -2 * ref_qp + 5 * -2 * ref_p, what + ": wrong result")) {
            return 1;
        }
    }
    return 0;
}

// Several Funcs and a residual. A term calling several of them goes to the
// first one's part, and common factors are hoisted through subtraction.
int multiple_funcs_and_residual_test() {
    const int W = 4, K = 9;
    Var x{"x"};
    RDom r(0, K, "r");
    Buffer<int> ab(W, K), bb(W, K), cb(W, K), sb(W), tb(W), kb(W);
    for (int i = 0; i < W; i++) {
        for (int k = 0; k < K; k++) {
            ab(i, k) = (i + 3 * k) % 7 - 3;
            bb(i, k) = (2 * i + k) % 5 - 2;
            cb(i, k) = (i * k) % 4 - 1;
        }
        sb(i) = i + 2;
        tb(i) = 3 - i;
        kb(i) = 7 * i - 4;
    }
    Func a{"a"}, b{"b"}, c{"c"}, s{"s"}, t{"t"}, k{"k"};
    a(x, _) = ab(x, _);
    b(x, _) = bb(x, _);
    c(x, _) = cb(x, _);
    s(x) = sb(x);
    t(x) = tb(x);
    k(x) = kb(x);

    Func f{"f"};
    f(x) = 0;
    f(x) += s(x) * a(x, r) * 2 + c(x, r) + b(x, r) * t(x) - a(x, r) * s(x) * 3 + k(x) - s(x) * b(x, r) * a(x, r);
    FuncVec intms = f.update().hoist_invariants({a, b});
    if (!check(intms.size() == 3 && intms[0].defined() && intms[1].defined() && intms[2].defined(),
               "expected parts for a and b and a residual") ||
        !check_mentions(intms[0].update_value(), {"a"}, {"s", "t", "c", "k"}, "a's part") ||
        !check_mentions(intms[1].update_value(), {"b"}, {"a", "s", "c", "k"}, "b's part") ||
        !check_mentions(intms[2].update_value(), {"c", "k"}, {"a", "b", "s", "t"}, "the residual") ||
        !check_mentions(f.update_value(), {"s", "t"}, {"a", "b", "c"}, "the write-back")) {
        return 1;
    }
    Buffer<int> out = f.realize({W});
    for (int i = 0; i < W; i++) {
        int ref = 0;
        for (int kk = 0; kk < K; kk++) {
            ref += sb(i) * ab(i, kk) * 2 + cb(i, kk) + bb(i, kk) * tb(i) -
                   ab(i, kk) * sb(i) * 3 + kb(i) - sb(i) * bb(i, kk) * ab(i, kk);
        }
        if (!check(out(i) == ref, "multiple funcs: wrong result at " + std::to_string(i))) {
            return 1;
        }
    }
    return 0;
}

// A part whose terms make no use of the RVars is still summed over the whole
// reduction domain, including its predicate.
int rvar_free_residual_test(bool predicated) {
    const int W = 3, K = 10;
    Var x{"x"};
    RDom r(0, K, "r");
    if (predicated) {
        r.where(r % 3 != 0);
    }
    Buffer<int> ab(W, K), kb(W);
    for (int i = 0; i < W; i++) {
        for (int k = 0; k < K; k++) {
            ab(i, k) = i * 5 + k - 4;
        }
        kb(i) = 11 * i - 6;
    }
    Func a{"a"}, k{"k"};
    a(x, _) = ab(x, _);
    k(x) = kb(x);

    Func f{"f"};
    f(x) = 1;
    f(x) += a(x, r) + k(x);
    FuncVec intms = f.update().hoist_invariants({a});
    if (!check(intms.size() == 2 && intms[1].defined(), "expected a residual")) {
        return 1;
    }
    Buffer<int> out = f.realize({W});
    for (int i = 0; i < W; i++) {
        int ref = 1;
        for (int kk = 0; kk < K; kk++) {
            if (!predicated || kk % 3 != 0) {
                ref += ab(i, kk) + kb(i);
            }
        }
        if (!check(out(i) == ref, "RVar-free residual: wrong result at " + std::to_string(i) + ": " +
                                      std::to_string(out(i)) + " vs " + std::to_string(ref))) {
            return 1;
        }
    }
    return 0;
}

// Integer arithmetic stays exact, wrapping included, through subtraction and
// multiplying out.
template<typename T>
int integer_exactness_test() {
    const int K = 23;
    Var x{"x"};
    RDom r(0, K, "r");
    Buffer<T> qb(K), pb(K), cb(K);
    for (int i = 0; i < K; i++) {
        qb(i) = (T)(i * 97 + 13);
        pb(i) = (T)(i * 61 - 200);
        cb(i) = (T)(i * 149 + 7);
    }
    Func q{"q"}, p{"p"}, c{"c"};
    q(x) = qb(x);
    p(x) = pb(x);
    c(x) = cb(x);
    const T d = (T)201, e = (T)-77, m = (T)99;

    Func f{"f"};
    f() = cast<T>(0);
    f() += (d * q(r) + m) * (e * p(r)) - c(r) * q(r) * d - (c(r) - m) * e;
    FuncVec intms = f.update().hoist_invariants({q, p});
    if (!check(intms.size() == 3 && intms[2].defined(), "integer exactness: expected a residual")) {
        return 1;
    }
    Buffer<T> out = f.realize();
    T ref = 0;
    for (int i = 0; i < K; i++) {
        ref = (T)(ref + (T)((T)((T)(d * qb(i)) + m) * (T)(e * pb(i))) - (T)((T)(cb(i) * qb(i)) * d) -
                  (T)((T)(cb(i) - m) * e));
    }
    if (!check(out() == ref, "integer exactness: wrong result")) {
        return 1;
    }
    return 0;
}

// The app's case: a block-scaled affine dot product, rfactored into per-block,
// per-lane partials. Isolating the codes q gives
//   dq * da * sum(q * a) + mq * da * sum(a)
// per block and lane, with the block factors hoisted out of both.
int rfactor_interplay_test(int n) {
    constexpr int block = 32, lane_width = 4;
    const int num_blocks = (n + block - 1) / block;
    Buffer<int8_t> qb(n), ab(n);
    Buffer<float> dqb(num_blocks), mqb(num_blocks), dab(num_blocks);
    for (int i = 0; i < n; i++) {
        qb(i) = (int8_t)((i * 7) % 16);
        ab(i) = (int8_t)((i * 5) % 11 - 5);
    }
    for (int b = 0; b < num_blocks; b++) {
        dqb(b) = (b + 1) * 0.5f;
        mqb(b) = (float)(b - 1);
        dab(b) = (b + 2) * 0.25f;
    }
    Var x{"x"}, u{"u"}, lane{"lane"};
    Func q{"q"}, a{"a"}, dq{"dq"}, mq{"mq"}, da{"da"};
    q(x) = qb(x);
    a(x) = ab(x);
    dq(x) = dqb(x);
    mq(x) = mqb(x);
    da(x) = dab(x);

    RDom r(0, n, "r");
    RVar ro{"ro"}, ri{"ri"}, rl{"rl"}, rw{"rw"};
    Func out{"out"};
    out() = 0.0f;
    out() += (dq(r / block) * cast<float>(q(r)) + mq(r / block)) * (da(r / block) * cast<float>(a(r)));
    out.update().split(r, ro, ri, block).split(ri, rl, rw, lane_width);
    Func blocks = out.update().rfactor({{rl, lane}, {ro, u}});
    blocks.bound(lane, 0, block / lane_width);
    FuncVec intms = blocks.update().hoist_invariants({q});
    if (!check(intms.size() == 2 && intms[1].defined(), "rfactor interplay: expected q's part and a residual") ||
        !check_mentions(intms[0].update_value(), {"q", "a"}, {"dq", "mq", "da"}, "rfactor interplay: q's part") ||
        !check_mentions(intms[1].update_value(), {"a"}, {"q", "dq", "mq", "da"}, "rfactor interplay: the residual") ||
        !check_mentions(blocks.update_value(), {"dq", "mq", "da"}, {"q", "a"}, "rfactor interplay: the write-back")) {
        return 1;
    }
    blocks.compute_root().vectorize(lane);
    blocks.update().vectorize(lane);
    for (Func intm : intms) {
        intm.compute_at(blocks, u).vectorize(lane);
        intm.update().vectorize(lane);
    }

    Buffer<float> result = out.realize();
    double ref = 0;
    for (int i = 0; i < n; i++) {
        const int b = i / block;
        ref += ((double)dqb(b) * qb(i) + mqb(b)) * ((double)dab(b) * ab(i));
    }
    if (!check(std::abs(result() - ref) <= 1e-3 * std::abs(ref),
               "rfactor interplay (n = " + std::to_string(n) + "): " + std::to_string(result()) + " vs " +
                   std::to_string(ref))) {
        return 1;
    }
    return 0;
}

// A specialization's update is split on its own; the unspecialized update is
// unchanged.
int specialization_test() {
    const int W = 5, K = 8;
    Var x{"x"};
    RDom r(0, K, "r");
    Buffer<int> qb(W, K), pb(W, K);
    for (int i = 0; i < W; i++) {
        for (int k = 0; k < K; k++) {
            qb(i, k) = (i + 2 * k) % 9 - 4;
            pb(i, k) = (3 * i + k) % 7 - 3;
        }
    }
    Func q{"q"}, p{"p"};
    q(x, _) = qb(x, _);
    p(x, _) = pb(x, _);
    Param<int> d{"d"}, m{"m"};
    Param<bool> flag{"flag"};
    d.set(4);
    m.set(-3);

    Func f{"f"};
    f(x) = 0;
    f(x) += (d * q(x, r) + m) * p(x, r);
    Stage s = f.update().specialize(flag);
    FuncVec intms = s.hoist_invariants({q});
    if (!check(intms.size() == 2 && intms[1].defined(), "specialization: expected q's part and a residual") ||
        !check_mentions(s.value(), {intms[0].name(), intms[1].name(), "d", "m"}, {"q", "p"},
                        "specialization: the write-back") ||
        !check_mentions(f.update().value(), {"q", "p", "d", "m"}, {}, "specialization: the unspecialized update")) {
        return 1;
    }
    for (bool b : {false, true}) {
        flag.set(b);
        Buffer<int> out = f.realize({W});
        for (int i = 0; i < W; i++) {
            int ref = 0;
            for (int k = 0; k < K; k++) {
                ref += (4 * qb(i, k) - 3) * pb(i, k);
            }
            if (!check(out(i) == ref, "specialization: wrong result")) {
                return 1;
            }
        }
    }
    return 0;
}

// A group of Funcs makes one part: the terms that call any of them.
int groups_test() {
    const int W = 4, K = 12;
    Var x{"x"};
    RDom r(0, K, "r");
    Buffer<int> ab(W, K), bb(W, K), cb(K), sb(W), tb(W);
    for (int i = 0; i < W; i++) {
        for (int k = 0; k < K; k++) {
            ab(i, k) = (i * 3 + k * 5) % 13 - 6;
            bb(i, k) = (i + k * 7) % 9 - 4;
        }
        sb(i) = 2 * i - 3;
        tb(i) = 5 - i;
    }
    for (int k = 0; k < K; k++) {
        cb(k) = (k * 7) % 10 - 4;
    }
    Func a{"a"}, b{"b"}, c{"c"}, s{"s"}, t{"t"};
    a(x, _) = ab(x, _);
    b(x, _) = bb(x, _);
    c(x) = cb(x);
    s(x) = sb(x);
    t(x) = tb(x);

    auto make = [&](const string &name) {
        Func f{name};
        f(x) = 0;
        f(x) += (s(x) * a(x, r) + t(x) * c(r)) * 3 + b(x, r) * s(x);
        return f;
    };
    Func f = make("f");
    FuncVec intms = f.update().hoist_invariants({{a, b}});
    if (!check(intms.size() == 2 && intms[1].defined(), "groups: expected a part and a residual") ||
        !check_mentions(intms[0].update_value(), {"a", "b"}, {"s", "t", "c"}, "groups: the group's part") ||
        !check_mentions(intms[1].update_value(), {"c"}, {"a", "b", "s", "t"}, "groups: the residual")) {
        return 1;
    }

    // A group after it takes the rest, so there is no residual.
    Func g = make("g");
    FuncVec gintms = g.update().hoist_invariants({{b, a}, {c}});
    if (!check(gintms.size() == 3 && gintms[1].defined() && !gintms[2].defined(),
               "groups: expected two parts and no residual") ||
        !check_mentions(gintms[0].update_value(), {"a", "b"}, {"s", "t", "c"}, "groups: the first group's part")) {
        return 1;
    }

    Buffer<int> f_out = f.realize({W}), g_out = g.realize({W});
    for (int i = 0; i < W; i++) {
        int ref = 0;
        for (int k = 0; k < K; k++) {
            ref += (sb(i) * ab(i, k) + tb(i) * cb(k)) * 3 + bb(i, k) * sb(i);
        }
        if (!check(f_out(i) == ref && g_out(i) == ref, "groups: wrong result")) {
            return 1;
        }
    }
    return 0;
}

// If every term calls the only group, there is nothing to split: the update is
// left as it is, and its own Func is the group's part.
int all_terms_test() {
    Inputs in;
    for (bool grouped : {false, true}) {
        Func f{"f"};
        f() = 0;
        f() += (in.d * in.q(in.r) + in.m * in.p(in.r)) * (in.e * in.q(in.r));
        const Expr before = f.update().value();
        FuncVec intms = grouped ? f.update().hoist_invariants({{in.p, in.q}}) :
                                  f.update().hoist_invariants({in.q});
        if (!check(intms.size() == 2 && intms[0].name() == f.name() && !intms[1].defined(),
                   "all terms: expected {f, undefined}") ||
            !check(equal(f.update().value(), before), "all terms: the update was changed")) {
            return 1;
        }
        Buffer<int> out = f.realize();
        const int ref = in.sum([&](int i) { return (3 * in.qb(i) + 5 * in.pb(i)) * (-2 * in.qb(i)); });
        if (!check(out() == ref, "all terms: wrong result")) {
            return 1;
        }
    }
    return 0;
}

int failure_modes_test() {
    Var x{"x"};
    RDom r(0, 8, "r");
    Func q{"q"}, p{"p"}, c{"c"}, z{"z"};
    q(x) = x;
    p(x) = x * 2;
    c(x) = x - 3;
    z(x) = x + 1;

    auto make = [&](const string &name) {
        Func f{name};
        f() = 0;
        f() += q(r) * p(r) + c(r);
        return f;
    };
    bool ok = true;
    ok &= expect_error([&] { make("f0").update().hoist_invariants({z}); }, "no term of the update calls " + z.name() + ":");
    ok &= expect_error([&] { make("f1").update().hoist_invariants({q, p}); },
                       "calls " + p.name() + " (other than terms an earlier group took)");
    ok &= expect_error([&] {
        Func f{"f2"};
        f() = 0;
        f() += q(r) * p(r);
        f.update().hoist_invariants({q, p});
    },
                       "calls " + p.name() + " (other than terms an earlier group took)");
    ok &= expect_error([&] { make("f3").update().hoist_invariants({}); },
                       "needs at least one group of Funcs to isolate");
    ok &= expect_error([&] { make("f9").update().hoist_invariants(vector<vector<Func>>{{q}, {}}); },
                       "was passed an empty group of Funcs");
    Func f4 = make("f4");
    ok &= expect_error([&] { f4.update().hoist_invariants({f4}); },
                       "cannot isolate the calls to " + f4.name() + " itself");
    ok &= expect_error([&] { Func f = make("f5"); Stage(f).hoist_invariants({q}); },
                       "must be called on an update definition");
    ok &= expect_error([&] {
        Func f{"f6"};
        f() = {0, 0};
        f() = {f()[0] + q(r), f()[1] + p(r)};
        f.update().hoist_invariants({q});
    },
                       "is Tuple-valued");
    ok &= expect_error([&] {
        Func f{"f7"};
        f() = 1;
        f() *= q(r) + p(r);
        f.update().hoist_invariants({q});
    },
                       "requires a sum reduction");
    ok &= expect_error([&] {
        Func f{"f8"};
        f() = 0;
        f() = max(f(), q(r) + p(r));
        f.update().hoist_invariants({q});
    },
                       "requires a sum reduction");
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char **argv) {
    struct Task {
        string desc;
        std::function<int()> fn;
    };

    vector<Task> tasks = {
        {"hoist_invariants(funcs) test (abadams example)", abadams_example_test},
        {"hoist_invariants(funcs) test (syntax invariance)", syntax_invariance_test},
        {"hoist_invariants(funcs) test (multiple funcs and residual)", multiple_funcs_and_residual_test},
        {"hoist_invariants(funcs) test (RVar-free residual)", [] { return rvar_free_residual_test(false); }},
        {"hoist_invariants(funcs) test (RVar-free residual, predicated)", [] { return rvar_free_residual_test(true); }},
        {"hoist_invariants(funcs) test (integer exactness, uint8)", integer_exactness_test<uint8_t>},
        {"hoist_invariants(funcs) test (integer exactness, int16)", integer_exactness_test<int16_t>},
        {"hoist_invariants(funcs) test (rfactor interplay, divisible)", [] { return rfactor_interplay_test(128); }},
        {"hoist_invariants(funcs) test (rfactor interplay, tail)", [] { return rfactor_interplay_test(100); }},
        {"hoist_invariants(funcs) test (specialization)", specialization_test},
        {"hoist_invariants(funcs) test (groups)", groups_test},
        {"hoist_invariants(funcs) test (all terms in one group)", all_terms_test},
        {"hoist_invariants(funcs) test (failure modes)", failure_modes_test},
    };

    using Sharder = Halide::Internal::Test::Sharder;
    Sharder sharder;
    for (size_t t = 0; t < tasks.size(); t++) {
        if (!sharder.should_run(t)) continue;
        const auto &task = tasks.at(t);
        std::cout << task.desc << "\n";
        if (task.fn() != 0) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
