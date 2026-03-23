#include "Halide.h"
#include "check_call_graphs.h"

#include <cstdio>
#include <map>

namespace {

using std::map;
using std::string;
using std::vector;

using namespace Halide;
using namespace Halide::Internal;

int calling_wrapper_no_op_test() {
    Var x("x"), y("y");

    {
        Func f("f"), g("g");
        f(x, y) = x + y;
        g(x, y) = f(x, y);

        // Calling wrap on the same Func for the same Func multiple times should
        // return the same wrapper
        Func wrapper = f.in(g);
        for (int i = 0; i < 5; ++i) {
            Func temp = f.in(g);
            if (wrapper.name() != temp.name()) {
                std::cerr << "Expect " << wrapper.name() << "; got " << temp.name() << " instead\n";
                return 1;
            }
        }
    }

    {
        Func f("f"), g("g");
        f(x, y) = x + y;
        g(x, y) = f(x, y);

        // Should return the same global wrapper
        Func wrapper1 = f.in();
        Func wrapper2 = f.in();
        if (wrapper1.name() != wrapper2.name()) {
            std::cerr << "Expect " << wrapper1.name() << "; got " << wrapper2.name() << " instead\n";
            return 1;
        }
    }

    {
        Func d("d"), e("e"), f("f"), g("g"), h("h");
        d(x, y) = x + y;
        e(x, y) = d(x, y);
        f(x, y) = d(x, y);
        g(x, y) = d(x, y);
        h(x, y) = d(x, y);

        Func wrapper1 = d.in({e, f, g});
        Func wrapper2 = d.in({g, f, e});
        if (wrapper1.name() != wrapper2.name()) {
            std::cerr << "Expect " << wrapper1.name() << "; got " << wrapper2.name() << " instead\n";
            return 1;
        }
    }

    return 0;
}

int func_wrapper_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x) = x;
    g(x, y) = f(x);

    Func wrapper = f.in(g).compute_root();
    f.compute_root();

    // Check the call graphs.
    // Expect 'g' to call 'wrapper', 'wrapper' to call 'f', 'f' to call nothing
    CallGraphs expected = {
        {g.name(), {wrapper.name()}},
        {wrapper.name(), {f.name()}},
        {f.name(), {}},
    };
    if (check_call_graphs(g, expected) != 0) {
        return 1;
    }

    Buffer<int> im = g.realize({200, 200});
    auto func = [](int x, int y) { return x; };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int multiple_funcs_sharing_wrapper_test() {
    Func f("f"), g1("g1"), g2("g2"), g3("g3");
    Var x("x"), y("y");

    f(x) = x;
    g1(x, y) = f(x);
    g2(x, y) = f(x);
    g3(x, y) = f(x);

    f.compute_root();
    Func f_wrapper = f.in({g1, g2}).compute_root();

    // Check the call graphs.
    // Expect 'g1' and 'g2' to call 'f_wrapper', 'g3' to call 'f',
    // f_wrapper' to call 'f', 'f' to call nothing
    Pipeline p({g1, g2, g3});
    CallGraphs expected = {
        {g1.name(), {f_wrapper.name()}},
        {g2.name(), {f_wrapper.name()}},
        {g3.name(), {f.name()}},
        {f_wrapper.name(), {f.name()}},
        {f.name(), {}},
    };
    if (check_call_graphs(p, expected) != 0) {
        return 1;
    }

    Realization r = p.realize({200, 200});
    Buffer<int> img1 = r[0];
    Buffer<int> img2 = r[1];
    Buffer<int> img3 = r[2];
    auto func = [](int x, int y) { return x; };
    if (check_image(img1, func)) {
        return 1;
    }
    if (check_image(img2, func)) {
        return 1;
    }
    if (check_image(img3, func)) {
        return 1;
    }
    return 0;
}

int global_wrapper_test() {
    Func f("f"), g("g"), h("h"), i("i");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(x, y);
    h(x, y) = g(x, y) + f(x, y);

    Var xi("xi"), yi("yi"), t("t");
    Func wrapper = f.in();
    f.compute_root();
    h.compute_root().tile(x, y, xi, yi, 16, 16).fuse(x, y, t).parallel(t);
    g.compute_at(h, yi);
    wrapper.compute_at(h, yi).tile(x, y, xi, yi, 8, 8).fuse(xi, yi, t).vectorize(t, 4);

    // Check the call graphs.
    // Expect 'g' to call 'wrapper', 'wrapper' to call 'f', 'f' to call nothing,
    // 'h' to call 'wrapper' and 'g'
    CallGraphs expected = {
        {h.name(), {g.name(), wrapper.name()}},
        {g.name(), {wrapper.name()}},
        {wrapper.name(), {f.name()}},
        {f.name(), {}},
    };
    if (check_call_graphs(h, expected) != 0) {
        return 1;
    }

    Buffer<int> im = h.realize({200, 200});
    auto func = [](int x, int y) { return 2 * (x + y); };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int update_defined_after_wrapper_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(x, y);

    Func wrapper = f.in(g);

    // Update of 'g' is defined after f.in(g) is called. g's updates should
    // still call f's wrapper.
    RDom r(0, 100, 0, 100);
    r.where(r.x < r.y);
    g(r.x, r.y) += 2 * f(r.x, r.y);

    Param<bool> param;

    Var xi("xi");
    RVar rxo("rxo"), rxi("rxi");
    g.specialize(param).vectorize(x, 8).unroll(x, 2).split(x, x, xi, 4).parallel(x);
    g.update(0).split(r.x, rxo, rxi, 2).unroll(rxi);
    f.compute_root();
    wrapper.compute_root().vectorize(x, 8).unroll(x, 2).split(x, x, xi, 4).parallel(x);

    CallGraphs expected = {
        {g.name(), {wrapper.name(), g.name()}},
        {wrapper.name(), {f.name()}},
        {f.name(), {}},
    };
    if (check_call_graphs(g, expected) != 0) {
        return 1;
    }

    for (bool param_value : {false, true}) {
        param.set(param_value);

        Buffer<int> im = g.realize({200, 200});
        auto func = [](int x, int y) {
            return ((0 <= x && x <= 99) && (0 <= y && y <= 99) && (x < y)) ? 3 * (x + y) : (x + y);
        };
        if (check_image(im, func)) {
            return 1;
        }
    }

    return 0;
}

int rdom_wrapper_test() {
    Func f("f"), g("g"), result("result");
    Var x("x"), y("y");

    constexpr int W = 32;
    constexpr int H = 32;

    f(x, y) = x + y;
    g(x, y) = 10;
    g(x, y) += 2 * f(x, x);
    RDom r(0, W, 0, H);
    g(r.x, r.y) += 3 * f(r.y, r.y);

    // Make a global wrapper on 'g', so that we can schedule initialization
    // and the update on the same compute level at the global wrapper
    Func wrapper = g.in().compute_root();
    g.compute_at(wrapper, x);
    f.compute_root();

    // Check the call graphs.
    // Expect 'wrapper' to call 'g', initialization of 'g' to call nothing
    // and its update to call 'f' and 'g', 'f' to call nothing
    CallGraphs expected = {
        {g.name(), {f.name(), g.name()}},
        {wrapper.name(), {g.name()}},
        {f.name(), {}},
    };
    if (check_call_graphs(wrapper, expected) != 0) {
        return 1;
    }

    Buffer<int> im = wrapper.realize({W, H});
    auto func = [](int x, int y) { return 4 * x + 6 * y + 10; };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int global_and_custom_wrapper_test() {
    Func f("f"), g("g"), result("result");
    Var x("x"), y("y");

    f(x) = x;
    g(x, y) = f(x);
    result(x, y) = f(x) + g(x, y);

    Func f_in_g = f.in(g).compute_at(g, x);
    Func f_wrapper = f.in().compute_at(result, y);
    f.compute_root();
    g.compute_at(result, y);

    // Check the call graphs.
    // Expect 'result' to call 'g' and 'f_wrapper', 'g' to call 'f_in_g',
    // 'f_wrapper' to call 'f', f_in_g' to call 'f', 'f' to call nothing

    CallGraphs expected = {
        {result.name(), {g.name(), f_wrapper.name()}},
        {g.name(), {f_in_g.name()}},
        {f_wrapper.name(), {f.name()}},
        {f_in_g.name(), {f.name()}},
        {f.name(), {}},
    };
    if (check_call_graphs(result, expected) != 0) {
        return 1;
    }

    Buffer<int> im = result.realize({200, 200});
    auto func = [](int x, int y) { return 2 * x; };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int wrapper_depend_on_mutated_func_test() {
    Func e("e"), f("f"), g("g"), h("h");
    Var x("x"), y("y");

    e(x, y) = x + y;
    f(x, y) = e(x, y);
    g(x, y) = f(x, y);
    h(x, y) = g(x, y);

    Var xo("xo"), xi("xi");
    e.compute_root();
    f.compute_at(g, y).vectorize(x, 8);
    g.compute_root();
    Func e_in_f = e.in(f);
    Func g_in_h = g.in(h).compute_root();
    g_in_h.compute_at(h, y).vectorize(x, 8);
    e_in_f.compute_at(f, y).split(x, xo, xi, 8);

    // Check the call graphs.
    // Expect 'h' to call 'g_in_h', 'g_in_h' to call 'g', 'g' to call 'f',
    // 'f' to call 'e_in_f', e_in_f' to call 'e', 'e' to call nothing
    CallGraphs expected = {
        {h.name(), {g_in_h.name()}},
        {g_in_h.name(), {g.name()}},
        {g.name(), {f.name()}},
        {f.name(), {e_in_f.name()}},
        {e_in_f.name(), {e.name()}},
        {e.name(), {}},
    };
    if (check_call_graphs(h, expected) != 0) {
        return 1;
    }

    Buffer<int> im = h.realize({200, 200});
    auto func = [](int x, int y) { return x + y; };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int wrapper_on_wrapper_test() {
    Func e("e"), f("f"), g("g"), h("h");
    Var x("x"), y("y");

    e(x, y) = x + y;
    f(x, y) = e(x, y);
    g(x, y) = f(x, y) + e(x, y);
    Func f_in_g = f.in(g).compute_root();
    Func f_in_f_in_g = f.in(f_in_g).compute_root();
    h(x, y) = g(x, y) + f(x, y) + f_in_f_in_g(x, y);

    e.compute_root();
    f.compute_root();
    g.compute_root();
    Func f_in_h = f.in(h).compute_root();
    Func g_in_h = g.in(h).compute_root();

    // Check the call graphs.
    CallGraphs expected = {
        {h.name(), {f_in_h.name(), g_in_h.name(), f_in_f_in_g.name()}},
        {f_in_h.name(), {f.name()}},
        {g_in_h.name(), {g.name()}},
        {g.name(), {e.name(), f_in_g.name()}},
        {f_in_g.name(), {f_in_f_in_g.name()}},
        {f_in_f_in_g.name(), {f.name()}},
        {f.name(), {e.name()}},
        {e.name(), {}},
    };
    if (check_call_graphs(h, expected) != 0) {
        return 1;
    }

    Buffer<int> im = h.realize({200, 200});
    auto func = [](int x, int y) { return 4 * (x + y); };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int wrapper_on_rdom_predicate_test() {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = 10;
    h(x, y) = 5;

    RDom r(0, 100, 0, 100);
    r.where(f(r.x, r.y) + h(r.x, r.y) < 50);
    g(r.x, r.y) += h(r.x, r.y);

    Func h_wrapper = h.in().store_root().compute_at(g, r.y);
    Func f_in_g = f.in(g).compute_at(g, r.x);
    f.compute_root();
    h.compute_root();

    // Check the call graphs.
    // Expect 'g' to call nothing, update of 'g' to call 'g', f_in_g', and 'h_wrapper',
    // 'f_in_g' to call 'f', 'f' to call nothing, 'h_wrapper' to call 'h', 'h' to call nothing
    CallGraphs expected = {
        {g.name(), {g.name(), f_in_g.name(), h_wrapper.name()}},
        {f_in_g.name(), {f.name()}},
        {f.name(), {}},
        {h_wrapper.name(), {h.name()}},
        {h.name(), {}},
    };
    if (check_call_graphs(g, expected) != 0) {
        return 1;
    }

    Buffer<int> im = g.realize({200, 200});
    auto func = [](int x, int y) {
        return ((0 <= x && x <= 99) && (0 <= y && y <= 99) && (x + y + 5 < 50)) ? 15 : 10;
    };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int two_fold_wrapper_test() {
    Func input("input"), input_in_output_in_output, input_in_output, output("output");
    Var x("x"), y("y");

    input(x, y) = 2 * x + 3 * y;
    input.compute_root();

    output(x, y) = input(y, x);

    Var xi("xi"), yi("yi");
    output.tile(x, y, xi, yi, 8, 8);

    input_in_output = input.in(output).compute_at(output, x).vectorize(x).unroll(y);
    input_in_output_in_output = input_in_output.in(output).compute_at(output, x).unroll(x).unroll(y);

    // Check the call graphs.
    CallGraphs expected = {
        {output.name(), {input_in_output_in_output.name()}},
        {input_in_output_in_output.name(), {input_in_output.name()}},
        {input_in_output.name(), {input.name()}},
        {input.name(), {}},
    };
    if (check_call_graphs(output, expected) != 0) {
        return 1;
    }

    Buffer<int> im = output.realize({1024, 1024});
    auto func = [](int x, int y) { return 3 * x + 2 * y; };
    if (check_image(im, func)) {
        return 1;
    }
    return 0;
}

int multi_folds_wrapper_test() {
    Func f("f"), f_in_g_in_g, f_in_g, f_in_g_in_g_in_h, f_in_g_in_g_in_h_in_h, g("g"), h("h");
    Var x("x"), y("y");

    f(x, y) = 2 * x + 3 * y;
    f.compute_root();

    g(x, y) = f(y, x);

    Var xi("xi"), yi("yi");
    g.compute_root().tile(x, y, xi, yi, 8, 8).vectorize(xi).unroll(yi);

    f_in_g = f.in(g).compute_root().tile(x, y, xi, yi, 8, 8).vectorize(xi).unroll(yi);
    f_in_g_in_g = f_in_g.in(g).compute_root().tile(x, y, xi, yi, 8, 8).unroll(xi).unroll(yi);

    h(x, y) = f_in_g_in_g(y, x);
    f_in_g_in_g_in_h = f_in_g_in_g.in(h).compute_at(h, x).vectorize(x).unroll(y);
    f_in_g_in_g_in_h_in_h = f_in_g_in_g_in_h.in(h).compute_at(h, x).unroll(x).unroll(y);
    h.compute_root().tile(x, y, xi, yi, 8, 8);

    Pipeline p({g, h});
    CallGraphs expected = {
        {g.name(), {f_in_g_in_g.name()}},
        {f_in_g_in_g.name(), {f_in_g.name()}},
        {f_in_g.name(), {f.name()}},
        {f.name(), {}},
        {h.name(), {f_in_g_in_g_in_h_in_h.name()}},
        {f_in_g_in_g_in_h_in_h.name(), {f_in_g_in_g_in_h.name()}},
        {f_in_g_in_g_in_h.name(), {f_in_g_in_g.name()}},
    };
    if (check_call_graphs(p, expected) != 0) {
        return 1;
    }

    Realization r = p.realize({1024, 1024});
    Buffer<int> img_g = r[0];
    Buffer<int> img_h = r[1];
    auto func = [](int x, int y) { return 3 * x + 2 * y; };
    if (check_image(img_g, func)) {
        return 1;
    }
    if (check_image(img_h, func)) {
        return 1;
    }
    return 0;
}

int lots_of_wrappers_test() {
    // This is a case that showed up in practice. It demonstrates that
    // it's important that the different wrappers of a Func get
    // different names.
    Func common;
    vector<Func> funcs;
    Var x;
    common(x) = x;
    common.compute_root();

    Func prev = common;
    for (int i = 0; i < 100; i++) {
        Func f;
        f(x) = common(x) + prev(x);
        prev = f;
        funcs.push_back(f);

        // Compute in groups of five, each with a local copy of the common func.
        if (i % 5 == 4) {
            vector<Func> group;
            funcs[i].compute_root();
            group.push_back(funcs[i]);
            for (int j = i - 4; j < i; j++) {
                funcs[j].compute_at(funcs[i], x);
                group.push_back(funcs[j]);
            }
            common.in(group).compute_at(funcs[i], x);
        }
    }

    // This used to crash
    funcs.back().compile_jit();
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    printf("Running calling wrap no op test\n");
    if (calling_wrapper_no_op_test() != 0) {
        return 1;
    }

    printf("Running func wrap test\n");
    if (func_wrapper_test() != 0) {
        return 1;
    }

    printf("Running multiple funcs sharing wrapper test\n");
    if (multiple_funcs_sharing_wrapper_test() != 0) {
        return 1;
    }

    printf("Running global wrap test\n");
    if (global_wrapper_test() != 0) {
        return 1;
    }

    printf("Running update is defined after wrap test\n");
    if (update_defined_after_wrapper_test() != 0) {
        return 1;
    }

    printf("Running rdom wrapper test\n");
    if (rdom_wrapper_test() != 0) {
        return 1;
    }

    printf("Running global + custom wrapper test\n");
    if (global_and_custom_wrapper_test() != 0) {
        return 1;
    }

    printf("Running wrapper depend on mutated func test\n");
    if (wrapper_depend_on_mutated_func_test() != 0) {
        return 1;
    }

    printf("Running wrapper on wrapper test\n");
    if (wrapper_on_wrapper_test() != 0) {
        return 1;
    }

    printf("Running wrapper on rdom predicate test\n");
    if (wrapper_on_rdom_predicate_test() != 0) {
        return 1;
    }

    printf("Running two fold wrapper test\n");
    if (two_fold_wrapper_test() != 0) {
        return 1;
    }

    printf("Running multi folds wrapper test\n");
    if (multi_folds_wrapper_test() != 0) {
        return 1;
    }

    printf("Running lots of wrappers test\n");
    if (lots_of_wrappers_test() != 0) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
