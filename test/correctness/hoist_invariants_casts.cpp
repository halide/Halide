#include "Halide.h"
#include "test_sharding.h"

#include <cstdio>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// hoist_invariants() looks through widening intrinsics and widening casts of
// sums where that separates terms, and only where that is exact.

namespace {

using std::string;
using std::vector;

using namespace Halide;
using namespace Halide::Internal;

bool mentions(const Expr &e, const string &name) {
    bool found = false;
    visit_with(
        e,
        [&](auto *self, const Call *op) {
            found |= op->name == name || starts_with(op->name, name + "$");
            self->visit_base(op);
        },
        [&](auto *self, const Variable *op) {
            found |= op->name == name;
        });
    return found;
}

bool has_intrinsic(const Expr &e, Call::IntrinsicOp intrinsic) {
    bool found = false;
    visit_with(e, [&](auto *self, const Call *op) {
        found |= op->is_intrinsic(intrinsic);
        self->visit_base(op);
    });
    return found;
}

bool check(bool ok, const string &what) {
    if (!ok) {
        printf("%s\n", what.c_str());
    }
    return ok;
}

bool check_mentions(const Expr &e, const vector<string> &yes, const vector<string> &no, const string &what) {
    bool ok = true;
    for (const string &n : yes) {
        std::ostringstream o;
        o << e;
        ok &= check(mentions(e, n), what + " does not mention " + n + ": " + o.str());
    }
    for (const string &n : no) {
        std::ostringstream o;
        o << e;
        ok &= check(!mentions(e, n), what + " mentions " + n + ": " + o.str());
    }
    return ok;
}

constexpr int N = 300;

// f() += i32(widening_mul(s, x(r))) hoists s: the widening_mul is the product
// of its operands widened.
int widening_mul_test() {
    Buffer<int8_t> xb(N);
    for (int i = 0; i < N; i++) {
        xb(i) = (int8_t)(i * 37 + 11);
    }
    Func x{"x"};
    Var v{"v"};
    x(v) = xb(v);
    Param<int8_t> s{"s"};
    s.set(-128);
    RDom r(0, N, "r");

    Func f{"f"};
    f() = 0;
    f() += cast<int>(widening_mul(s, x(r)));
    FuncVec intms = f.update().hoist_invariants();
    if (!check(intms.size() == 1, "widening_mul: expected one intermediate") ||
        !check_mentions(intms[0].update_value(), {"x"}, {"s"}, "widening_mul: the intermediate") ||
        !check_mentions(f.update_value(), {"s", intms[0].name()}, {"x"}, "widening_mul: the write-back")) {
        return 1;
    }
    int ref = 0;
    for (int i = 0; i < N; i++) {
        ref += -128 * xb(i);
    }
    Buffer<int> out = f.realize();
    if (!check(out() == ref, "widening_mul: wrong result")) {
        return 1;
    }

    // A widening_add under max hoists the same way. (max_k(c + x_k) =
    // c + max_k(x_k) needs a type whose addition cannot wrap.)
    Func g{"g"};
    g() = Int(32).min();
    g() = max(g(), cast<int>(widening_add(s, x(r))));
    intms = g.update().hoist_invariants();
    if (!check(intms.size() == 1, "widening_add: expected one intermediate") ||
        !check_mentions(intms[0].update_value(), {"x"}, {"s"}, "widening_add: the intermediate")) {
        return 1;
    }
    int gref = std::numeric_limits<int>::min();
    for (int i = 0; i < N; i++) {
        gref = std::max(gref, -128 + xb(i));
    }
    Buffer<int> gout = g.realize();
    if (!check(gout() == gref, "widening_add: wrong result")) {
        return 1;
    }

    // Where nothing needs separating, a widening_mul keeps its form.
    Func h{"h"};
    h() = 0;
    h() += cast<int>(widening_mul(x(r), x(N - 1 - r))) * s;
    intms = h.update().hoist_invariants();
    if (!check(intms.size() == 1 && has_intrinsic(intms[0].update_value(), Call::widening_mul),
               "widening_mul: a widening_mul of two dependent operands was rewritten")) {
        return 1;
    }
    int href = 0;
    for (int i = 0; i < N; i++) {
        href += xb(i) * xb(N - 1 - i) * -128;
    }
    Buffer<int> hout = h.realize();
    if (!check(hout() == href, "widening_mul: wrong result with no separation")) {
        return 1;
    }
    return 0;
}

// The codes of a quantized format: a 2-bit field minus an offset, widened to
// float. With the decoder eager-inlined, hoist_invariants({field}) splits the
// offset's term off the codes', since field is in [0, 3] and so the int8 sum
// cannot wrap.
int offset_split_test() {
    Buffer<uint32_t> packed(N);
    Buffer<float> actb(N);
    for (int i = 0; i < N; i++) {
        packed(i) = (uint32_t)i * 2654435761u;
        actb(i) = (float)((i * 13) % 255 - 127);
    }
    Var v{"v"};
    Func field{"field"}, code{"code"}, act{"act"};
    field(v) = cast<uint8_t>((packed(v) >> 7) & 3);
    code(v) = cast<int8_t>(field(v)) + cast<int8_t>(-1);
    act(v) = actb(v);
    RDom r(0, N, "r");

    Func f{"f"};
    f() = 0.0f;
    f() += cast<float>(code(r)) * act(r);
    f.update().eager_inline(code);
    FuncVec intms = f.update().hoist_invariants({field});
    if (!check(intms.size() == 2 && intms[0].defined() && intms[1].defined(),
               "offset split: expected the field's part and a residual") ||
        !check_mentions(intms[0].update_value(), {"field", "act"}, {}, "offset split: the field's part") ||
        !check_mentions(intms[1].update_value(), {"act"}, {"field"}, "offset split: the residual")) {
        return 1;
    }
    float ref = 0;
    for (int i = 0; i < N; i++) {
        ref += (float)((int)((packed(i) >> 7) & 3) - 1) * actb(i);
    }
    Buffer<float> out = f.realize();
    if (!check(out() == ref, "offset split: wrong result")) {
        return 1;
    }

    // The same for an integer widening, where a uint8 sum is proven in range.
    Func g{"g"};
    g() = cast<int16_t>(0);
    g() += cast<int16_t>(field(r) + cast<uint8_t>(200)) * cast<int16_t>(r);
    intms = g.update().hoist_invariants({field});
    if (!check(intms.size() == 2 && intms[0].defined() && intms[1].defined(),
               "uint8 sum split: expected the field's part and a residual") ||
        !check_mentions(intms[1].update_value(), {}, {"field"}, "uint8 sum split: the residual")) {
        return 1;
    }
    int16_t gref = 0;
    for (int i = 0; i < N; i++) {
        gref = (int16_t)(gref + (int16_t)(((packed(i) >> 7) & 3) + 200) * (int16_t)i);
    }
    Buffer<int16_t> gout = g.realize();
    if (!check(gout() == gref, "uint8 sum split: wrong result")) {
        return 1;
    }
    return 0;
}

// When the narrow sum can wrap, cast(x + c) is not cast(x) + cast(c), so the
// cast is a single term and is not split.
int wrapping_test() {
    Buffer<uint8_t> wb(N);
    Buffer<float> actb(N);
    for (int i = 0; i < N; i++) {
        wb(i) = (uint8_t)(i * 7);
        actb(i) = (float)((i * 13) % 255 - 127);
    }
    wb(0) = 127;
    Var v{"v"};
    Func w{"w"}, act{"act"};
    w(v) = wb(v);
    act(v) = actb(v);
    RDom r(0, N, "r");

    Func f{"f"};
    f() = 0.0f;
    f() += cast<float>(cast<int8_t>(w(r)) + cast<int8_t>(1)) * act(r);
    FuncVec intms = f.update().hoist_invariants({w});
    if (!check(intms.size() == 2 && intms[0].defined() && !intms[1].defined(),
               "wrapping: the cast was split")) {
        return 1;
    }
    float ref = 0;
    for (int i = 0; i < N; i++) {
        ref += (float)(int8_t)((int8_t)wb(i) + 1) * actb(i);
    }
    Buffer<float> out = f.realize();
    if (!check(out() == ref, "wrapping: wrong result")) {
        return 1;
    }

    // Likewise for an integer widening of a uint8 sum.
    Func g{"g"};
    g() = cast<int16_t>(0);
    g() += cast<int16_t>(w(r) + cast<uint8_t>(200)) * cast<int16_t>(r);
    intms = g.update().hoist_invariants({w});
    if (!check(intms.size() == 2 && intms[0].defined() && !intms[1].defined(),
               "wrapping: the uint8 sum was split")) {
        return 1;
    }
    int16_t gref = 0;
    for (int i = 0; i < N; i++) {
        gref = (int16_t)(gref + (int16_t)(uint8_t)(wb(i) + 200) * (int16_t)i);
    }
    Buffer<int16_t> gout = g.realize();
    if (!check(gout() == gref, "wrapping: wrong integer result")) {
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    struct Task {
        string desc;
        std::function<int()> fn;
    };

    vector<Task> tasks = {
        {"hoist_invariants casts test (widening intrinsics)", widening_mul_test},
        {"hoist_invariants casts test (offset split)", offset_split_test},
        {"hoist_invariants casts test (wrapping)", wrapping_test},
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
