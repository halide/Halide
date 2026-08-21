#include "Halide.h"
#include "check_call_graphs.h"

#include <cstdio>

using namespace Halide;

namespace {

// Build a small pipeline with anonymous intermediate Funcs, similar in shape
// to local_laplacian's pyramid: we want to call clone_in / in on a non-direct
// caller and have the wrapper be inserted along all paths from that caller
// down to the wrapped Func.
int transitive_clone_in_test() {
    Var x("x"), y("y");

    Func base("base");
    base(x, y) = x + y;

    // Two anonymous helpers that each directly call base.
    Func helper_a, helper_b;
    helper_a(x, y) = base(x, y) + 1;
    helper_b(x, y) = base(x, y) * 2;

    // top transitively calls base via the helpers, but does not directly.
    Func top("top");
    top(x, y) = helper_a(x, y) + helper_b(x, y);

    // sibling also uses base directly but is *not* on the path from top.
    Func sibling("sibling");
    sibling(x, y) = base(x, y) - 1;

    // Cloning base into top should expand to {helper_a, helper_b}, but must
    // leave sibling's call to base untouched.
    Func cloned = base.clone_in(top);

    Func out("out");
    out(x, y) = top(x, y) + sibling(x, y);

    base.compute_root();
    helper_a.compute_root();
    helper_b.compute_root();
    cloned.compute_root();
    sibling.compute_root();
    top.compute_root();

    // First check: numerical correctness.
    Pipeline p(out);
    Buffer<int> result = p.realize({16, 16});
    auto check = [](int xv, int yv) {
        int b = xv + yv;
        int ha = b + 1;
        int hb = b * 2;
        int t = ha + hb;
        int s = b - 1;
        return t + s;
    };
    if (check_image2(result, check) != 0) {
        return 1;
    }

    // Second check: helper_a and helper_b should load from the clone, not
    // base; sibling should still load from base.
    CheckCalls *checker = new CheckCalls;
    Pipeline p2(out);
    p2.add_custom_lowering_pass(checker);
    p2.compile_to_module(p2.infer_arguments(), "");
    const auto &calls = checker->calls;

    auto loads_from = [&](const std::string &producer, const std::string &callee) {
        auto it = calls.find(producer);
        if (it == calls.end()) {
            printf("Producer %s not found\n", producer.c_str());
            return false;
        }
        for (const std::string &c : it->second) {
            if (c == callee) return true;
        }
        return false;
    };

    if (loads_from(helper_a.name(), base.name())) {
        printf("helper_a should not directly call base after clone_in\n");
        return 1;
    }
    if (loads_from(helper_b.name(), base.name())) {
        printf("helper_b should not directly call base after clone_in\n");
        return 1;
    }
    if (!loads_from(helper_a.name(), cloned.name())) {
        printf("helper_a should call the clone\n");
        return 1;
    }
    if (!loads_from(helper_b.name(), cloned.name())) {
        printf("helper_b should call the clone\n");
        return 1;
    }
    if (!loads_from(sibling.name(), base.name())) {
        printf("sibling should still call base\n");
        return 1;
    }

    return 0;
}

// Direct callers passed to clone_in should still work (no expansion needed).
int direct_clone_in_still_works_test() {
    Var x("x"), y("y");
    Func f("f"), g("g");
    f(x, y) = x + y;
    g(x, y) = f(x, y) + 7;
    Func cloned = f.clone_in(g);
    f.compute_root();
    cloned.compute_root();
    Buffer<int> r = g.realize({8, 8});
    return check_image2(r, [](int xv, int yv) { return xv + yv + 7; });
}

// in() is also transitive.
int transitive_in_test() {
    Var x("x"), y("y");
    Func base("base");
    base(x, y) = x + y;
    Func mid;
    mid(x, y) = base(x, y) + 3;
    Func top("top");
    top(x, y) = mid(x, y) * 2;

    // base.in(top) should resolve to base.in(mid).
    Func wrapper = base.in(top);

    base.compute_root();
    mid.compute_root();
    wrapper.compute_root();
    top.compute_root();

    Buffer<int> r = top.realize({8, 8});
    if (check_image2(r, [](int xv, int yv) { return (xv + yv + 3) * 2; }) != 0) {
        return 1;
    }

    CheckCalls *checker = new CheckCalls;
    Pipeline p(top);
    p.add_custom_lowering_pass(checker);
    p.compile_to_module(p.infer_arguments(), "");
    const auto &calls = checker->calls;
    auto it = calls.find(mid.name());
    if (it == calls.end()) {
        printf("mid not found in call graph\n");
        return 1;
    }
    for (const auto &c : it->second) {
        if (c == base.name()) {
            printf("mid should not directly call base after in()\n");
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    printf("Running transitive_clone_in_test\n");
    if (transitive_clone_in_test() != 0) {
        printf("transitive_clone_in_test failed\n");
        return 1;
    }
    printf("Running direct_clone_in_still_works_test\n");
    if (direct_clone_in_still_works_test() != 0) {
        printf("direct_clone_in_still_works_test failed\n");
        return 1;
    }
    printf("Running transitive_in_test\n");
    if (transitive_in_test() != 0) {
        printf("transitive_in_test failed\n");
        return 1;
    }
    printf("Success!\n");
    return 0;
}
