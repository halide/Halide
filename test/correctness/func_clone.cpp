#include "Halide.h"
#include "check_call_graphs.h"

#include <cstdio>
#include <map>

namespace {

using std::map;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

int calling_clone_no_op_test() {
    Var x("x"), y("y");

    {
        Func f("f"), g("g");
        f(x, y) = x + y;
        g(x, y) = f(x, y);

        // Calling clone on the same Func for the same Func multiple times should
        // return the same clone
        Func clone = f.clone_in(g);
        for (int i = 0; i < 5; ++i) {
            Func temp = f.clone_in(g);
            if (clone.name() != temp.name()) {
                std::cerr << "Expect " << clone.name() << "; got " << temp.name() << " instead\n";
                return -1;
            }
        }
    }

    {
        Func d("d"), e("e"), f("f"), g("g"), h("h");
        d(x, y) = x + y;
        e(x, y) = d(x, y);
        f(x, y) = d(x, y);
        g(x, y) = d(x, y);
        h(x, y) = d(x, y);

        Func clone1 = d.clone_in({e, f, g});
        Func clone2 = d.clone_in({g, f, e});
        if (clone1.name() != clone2.name()) {
            std::cerr << "Expect " << clone1.name() << "; got " << clone2.name() << " instead\n";
            return -1;
        }
    }

    return 0;
}

int func_clone_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x) = x;
    g(x, y) = f(x);

    Func clone = f.clone_in(g).compute_root();
    f.compute_root();

    // Check the call graphs.
    // Expect 'g' to call 'clone', 'clone' to call nothing, and 'f' not
    // in the final IR.
    CallGraphs expected = {
        {g.name(), {clone.name()}},
        {clone.name(), {}},
    };
    if (check_call_graphs(g, expected) != 0) {
        return -1;
    }

    Buffer<int> im = g.realize({200, 200});
    auto func = [](int x, int y) { return x; };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int multiple_funcs_sharing_clone_test() {
    Func f("f"), g1("g1"), g2("g2"), g3("g3");
    Var x("x"), y("y");

    f(x) = x;
    g1(x, y) = f(x);
    g2(x, y) = f(x);
    g3(x, y) = f(x);

    f.compute_root();
    Func f_clone = f.clone_in({g1, g2}).compute_root();

    // Check the call graphs.
    // Expect 'g1' and 'g2' to call 'f_clone', 'g3' to call 'f',
    // f_clone' to call nothing, 'f' to call nothing
    Pipeline p({g1, g2, g3});
    CallGraphs expected = {
        {g1.name(), {f_clone.name()}},
        {g2.name(), {f_clone.name()}},
        {g3.name(), {f.name()}},
        {f_clone.name(), {}},
        {f.name(), {}},
    };
    if (check_call_graphs(p, expected) != 0) {
        return -1;
    }

    Realization r = p.realize({200, 200});
    Buffer<int> img1 = r[0];
    Buffer<int> img2 = r[1];
    Buffer<int> img3 = r[2];
    auto func = [](int x, int y) { return x; };
    if (check_image(img1, func)) {
        return -1;
    }
    if (check_image(img2, func)) {
        return -1;
    }
    if (check_image(img3, func)) {
        return -1;
    }
    return 0;
}

int update_defined_after_clone_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(x, y);

    Func clone = f.clone_in(g);

    // Update of 'g' is defined after f.clone_in(g) is called. g's updates should
    // still call f's clone.
    RDom r(0, 100, 0, 100);
    r.where(r.x < r.y);
    g(r.x, r.y) += 2 * f(r.x, r.y);

    Param<bool> param;

    Var xi("xi");
    RVar rxo("rxo"), rxi("rxi");
    g.specialize(param).vectorize(x, 8).unroll(x, 2).split(x, x, xi, 4).parallel(x);
    g.update(0).split(r.x, rxo, rxi, 2).unroll(rxi);
    f.compute_root();
    clone.compute_root().vectorize(x, 8).unroll(x, 2).split(x, x, xi, 4).parallel(x);

    // Check the call graphs.
    // Expect initialization of 'g' to call 'clone' and its update to call
    // 'clone' and 'g', clone' to call nothing, and 'f' not in the final IR.
    CallGraphs expected = {
        {g.name(), {clone.name(), g.name()}},
        {clone.name(), {}},
    };
    if (check_call_graphs(g, expected) != 0) {
        return -1;
    }

    Buffer<int> im = g.realize({200, 200});
    auto func = [](int x, int y) {
        return ((0 <= x && x <= 99) && (0 <= y && y <= 99) && (x < y)) ? 3 * (x + y) : (x + y);
    };
    if (check_image(im, func)) {
        return -1;
    }

    for (bool param_value : {false, true}) {
        param.set(param_value);

        Buffer<int> im = g.realize({200, 200});
        auto func = [](int x, int y) {
            return ((0 <= x && x <= 99) && (0 <= y && y <= 99) && (x < y)) ? 3 * (x + y) : (x + y);
        };
        if (check_image(im, func)) {
            return -1;
        }
    }

    return 0;
}

int clone_depend_on_mutated_func_test() {
    Func a("a"), b("b"), c("c"), d("d"), e("e"), f("f");
    Var x("x"), y("y");

    a(x, y) = x + y;
    b(x, y) = a(x, y) + 1;
    e(x, y) = a(x, y) + 2;
    c(x, y) = b(x, y) + 2;
    d(x, y) = c(x, y) + 3;
    f(x, y) = c(x, y) + 4;

    Func a_clone_in_b = a.clone_in(b).compute_root();
    Func c_clone_in_f = c.clone_in(f).compute_root();

    a.compute_root();
    b.compute_root();
    c.compute_root();
    d.compute_root();
    e.compute_root();
    f.compute_root();

    // Check the call graphs.
    Pipeline p({d, e, f});
    CallGraphs expected = {
        {e.name(), {a.name()}},
        {a.name(), {}},
        {d.name(), {c.name()}},
        {f.name(), {c_clone_in_f.name()}},
        {c.name(), {b.name()}},
        {c_clone_in_f.name(), {b.name()}},
        {b.name(), {a_clone_in_b.name()}},
        {a_clone_in_b.name(), {}},
    };
    if (check_call_graphs(p, expected) != 0) {
        return -1;
    }

    Realization r = p.realize({25, 25});
    Buffer<int> img_d = r[0];
    Buffer<int> img_e = r[1];
    Buffer<int> img_f = r[2];

    auto func_d = [](int x, int y) { return x + y + 6; };
    if (check_image(img_d, func_d)) {
        return -1;
    }
    auto func_e = [](int x, int y) { return x + y + 2; };
    if (check_image(img_e, func_e)) {
        return -1;
    }
    auto func_f = [](int x, int y) { return x + y + 7; };
    if (check_image(img_f, func_f)) {
        return -1;
    }
    return 0;
}

int clone_on_clone_test() {
    Func a("a"), b("b"), c("c"), d("d"), e("e"), f("f");
    Var x("x"), y("y");

    a(x, y) = x + y;
    b(x, y) = a(x, y) + 1;
    c(x, y) = b(x, y) + 2;
    d(x, y) = b(x, y) + 3;
    e(x, y) = a(x, y) + b(x, y);
    f(x, y) = a(x, y) + b(x, y) + 1;

    Func b_clone_in_d_f = b.clone_in({d, f}).compute_root();
    Func a_clone_in_b_e = a.clone_in({b, e}).compute_root();
    Func a_clone_in_b_e_in_e = a_clone_in_b_e.clone_in(e).compute_root();

    a.compute_root();
    b.compute_root();
    c.compute_root();
    d.compute_root();
    e.compute_root();
    f.compute_root();

    // Check the call graphs.
    Pipeline p({c, d, e, f});
    CallGraphs expected = {
        {e.name(), {b.name(), a_clone_in_b_e_in_e.name()}},
        {c.name(), {b.name()}},
        {b.name(), {a_clone_in_b_e.name()}},
        {a_clone_in_b_e.name(), {}},
        {a_clone_in_b_e_in_e.name(), {}},
        {d.name(), {b_clone_in_d_f.name()}},
        {f.name(), {b_clone_in_d_f.name(), a.name()}},
        {b_clone_in_d_f.name(), {a.name()}},
        {a.name(), {}},
    };
    if (check_call_graphs(p, expected) != 0) {
        return -1;
    }

    Realization r = p.realize({25, 25});
    Buffer<int> img_c = r[0];
    Buffer<int> img_d = r[1];
    Buffer<int> img_e = r[2];
    Buffer<int> img_f = r[3];

    auto func_c = [](int x, int y) { return x + y + 3; };
    if (check_image(img_c, func_c)) {
        return -1;
    }
    auto func_d = [](int x, int y) { return x + y + 4; };
    if (check_image(img_d, func_d)) {
        return -1;
    }
    auto func_e = [](int x, int y) { return 2 * x + 2 * y + 1; };
    if (check_image(img_e, func_e)) {
        return -1;
    }
    auto func_f = [](int x, int y) { return 2 * x + 2 * y + 2; };
    if (check_image(img_f, func_f)) {
        return -1;
    }
    return 0;
}

int clone_reduction_test() {
    // Check that recursive references from a Func back to itself get
    // rewritten too in a clone. This schedule would be illegal if
    // they did not.

    RDom r(0, 8);
    Var x;
    Func sum;
    sum(x) += r * x;

    Func f, g;

    f(x) = sum(x);
    g(x) = sum(x);

    sum.clone_in(g).compute_at(g, x);
    sum.compute_at(f, x);

    Pipeline p({f, g});
    p.realize({128});

    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    printf("Running calling clone no op test\n");
    if (calling_clone_no_op_test() != 0) {
        return -1;
    }

    printf("Running func clone test\n");
    if (func_clone_test() != 0) {
        return -1;
    }

    printf("Running multiple funcs sharing clone test\n");
    if (multiple_funcs_sharing_clone_test() != 0) {
        return -1;
    }

    printf("Running update is defined after clone test\n");
    if (update_defined_after_clone_test() != 0) {
        return -1;
    }

    printf("Running clone depend on mutated func test\n");
    if (clone_depend_on_mutated_func_test() != 0) {
        return -1;
    }

    printf("Running clone on clone test\n");
    if (clone_on_clone_test() != 0) {
        return -1;
    }

    printf("Running clone reduction test\n");
    if (clone_reduction_test() != 0) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
