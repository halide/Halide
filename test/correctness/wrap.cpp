#include "Halide.h"
#include <assert.h>
#include <stdio.h>
#include <algorithm>
#include <functional>
#include <map>
#include <numeric>

using std::map;
using std::vector;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

typedef map<string, vector<string>> CallGraphs;

class CheckCalls : public IRVisitor {
public:
    CallGraphs calls; // Caller -> vector of callees
    string producer = "";
private:
    using IRVisitor::visit;

    void visit(const ProducerConsumer *op) {
        string old_producer = producer;
        producer = op->name;
        calls[producer]; // Make sure each producer is allocated a slot
        op->produce.accept(this);
        producer = old_producer;

        if (op->update.defined()) {
            // Just lump all the update stages together
            producer = op->name + ".update(" + std::to_string(0) + ")";
            calls[producer]; // Make sure each producer is allocated a slot
            op->update.accept(this);
            producer = old_producer;
        }
        op->consume.accept(this);
        producer = old_producer;
    }

    void visit(const Load *op) {
        IRVisitor::visit(op);
        if (!producer.empty()) {
            assert(calls.count(producer) > 0);
            vector<string> &callees = calls[producer];
            if(std::find(callees.begin(), callees.end(), op->name) == callees.end()) {
                callees.push_back(op->name);
            }
        }
    }
};


int check_call_graphs(CallGraphs &result, CallGraphs &expected) {
    if (result.size() != expected.size()) {
        printf("Expect %d callers instead of %d\n", (int)expected.size(), (int)result.size());
        return -1;
    }
    for (auto &iter : expected) {
        if (result.count(iter.first) == 0) {
            printf("Expect %s to be in the call graphs\n", iter.first.c_str());
            return -1;
        }
        vector<string> &expected_callees = iter.second;
        vector<string> &result_callees = result[iter.first];
        std::sort(expected_callees.begin(), expected_callees.end());
        std::sort(result_callees.begin(), result_callees.end());
        if (expected_callees != result_callees) {
            string expected_str = std::accumulate(
                expected_callees.begin(), expected_callees.end(), std::string{},
                [](const string &a, const string &b) {
                    return a.empty() ? b : a + ", " + b;
                });
            string result_str = std::accumulate(
                result_callees.begin(), result_callees.end(), std::string{},
                [](const string &a, const string &b) {
                    return a.empty() ? b : a + ", " + b;
                });

            printf("Expect calless of %s to be (%s); got (%s) instead\n",
                    iter.first.c_str(), expected_str.c_str(), result_str.c_str());
            return -1;
        }

    }
    return 0;
}

int check_image(const Image<int> &im, const std::function<int(int,int)> &func) {
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = func(x, y);
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    return 0;
}

int func_wrap_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x) = x;
    g(x, y) = f(x);

    Func wrapper = f.in(g).compute_root();
    f.compute_root();

    // Check the call graphs.
    // Expect 'g' to call 'wrapper', 'wrapper' to call 'f', 'f' to call nothing
    Module m = g.compile_to_module({});
    CheckCalls c;
    m.functions[0].body.accept(&c);

    CallGraphs expected = {
        {g.name(), {wrapper.name()}},
        {wrapper.name(), {f.name()}},
        {f.name(), {}},
    };
    if (check_call_graphs(c.calls, expected) != 0) {
        return -1;
    }

    Image<int> im = g.realize(200, 200);
    auto func = [](int x, int y) { return x; };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int global_wrap_test() {
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
    Module m = h.compile_to_module({});
    CheckCalls c;
    m.functions[0].body.accept(&c);

    CallGraphs expected = {
        {h.name(), {g.name(), wrapper.name()}},
        {g.name(), {wrapper.name()}},
        {wrapper.name(), {f.name()}},
        {f.name(), {}},
    };
    if (check_call_graphs(c.calls, expected) != 0) {
        return -1;
    }

    Image<int> im = h.realize(200, 200);
    auto func = [](int x, int y) { return 2*(x + y); };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int update_defined_after_wrap_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(x, y);

    Func wrapper = f.in(g);

    // Update of 'g' is defined after f.in(g) is called. g's updates should
    // still call f's wrapper.
    RDom r(0, 100, 0, 100);
    r.where(r.x < r.y);
    g(r.x, r.y) += 2*f(r.x, r.y);

    Param<bool> param;

    Var xi("xi");
    RVar rxo("rxo"), rxi("rxi");
    g.specialize(param).vectorize(x, 8).unroll(x, 2).split(x, x, xi, 4).parallel(x);
    g.update(0).split(r.x, rxo, rxi, 2).unroll(rxi);
    f.compute_root();
    wrapper.compute_root().vectorize(x, 8).unroll(x, 2).split(x, x, xi, 4).parallel(x);

    {
        param.set(true);

        // Check the call graphs.
        // Expect initialization of 'g' to call 'wrapper' and its update to call
        // 'wrapper' and 'g', wrapper' to call 'f', 'f' to call nothing
        Module m = g.compile_to_module({g.infer_arguments()});
        CheckCalls c;
        m.functions[0].body.accept(&c);

        CallGraphs expected = {
            {g.name(), {wrapper.name()}},
            {g.update(0).name(), {wrapper.name(), g.name()}},
            {wrapper.name(), {f.name()}},
            {f.name(), {}},
        };
        if (check_call_graphs(c.calls, expected) != 0) {
            return -1;
        }

        Image<int> im = g.realize(200, 200);
        auto func = [](int x, int y) {
            return ((0 <= x && x <= 99) && (0 <= y && y <= 99) && (x < y)) ? 3*(x + y) : (x + y);
        };
        if (check_image(im, func)) {
            return -1;
        }
    }

    {
        param.set(false);

        // Check the call graphs.
        // Expect initialization of 'g' to call 'wrapper' and its update to call
        // 'wrapper' and 'g', wrapper' to call 'f', 'f' to call nothing
        Module m = g.compile_to_module({g.infer_arguments()});
        CheckCalls c;
        m.functions[0].body.accept(&c);

        CallGraphs expected = {
            {g.name(), {wrapper.name()}},
            {g.update(0).name(), {wrapper.name(), g.name()}},
            {wrapper.name(), {f.name()}},
            {f.name(), {}},
        };
        if (check_call_graphs(c.calls, expected) != 0) {
            return -1;
        }

        Image<int> im = g.realize(200, 200);
        auto func = [](int x, int y) {
            return ((0 <= x && x <= 99) && (0 <= y && y <= 99) && (x < y)) ? 3*(x + y) : (x + y);
        };
        if (check_image(im, func)) {
            return -1;
        }
    }

    return 0;
}

int rdom_wrapper_test() {
    // Scheduling initialization + update on the same compute level using wrapper
    Func f("f"), g("g"), result("result");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = 10;
    g(x, y) += 2 * f(x, x);
    g(x, y) += 3 * f(y, y);
    result(x, y) = g(x, y) + 20;

    Func wrapper = g.in(result).compute_at(result, x);
    f.compute_root();

    // Check the call graphs.
    // Expect 'result' to call 'wrapper', initialization of 'g' to call nothing
    // and its update to call 'f' and 'g', wrapper' to call 'g', 'f' to call nothing
    Module m = result.compile_to_module({});
    CheckCalls c;
    m.functions[0].body.accept(&c);

    CallGraphs expected = {
        {result.name(), {wrapper.name()}},
        {g.name(), {}},
        {g.update(0).name(), {f.name(), g.name()}},
        {wrapper.name(), {g.name()}},
        {f.name(), {}},
    };
    if (check_call_graphs(c.calls, expected) != 0) {
        return -1;
    }

    Image<int> im = result.realize(200, 200);
    auto func = [](int x, int y) { return 4*x + 6* y + 30; };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int global_and_custom_wrap_test() {
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
    Module m = result.compile_to_module({});
    CheckCalls c;
    m.functions[0].body.accept(&c);

    CallGraphs expected = {
        {result.name(), {g.name(), f_wrapper.name()}},
        {g.name(), {f_in_g.name()}},
        {f_wrapper.name(), {f.name()}},
        {f_in_g.name(), {f.name()}},
        {f.name(), {}},
    };
    if (check_call_graphs(c.calls, expected) != 0) {
        return -1;
    }

    Image<int> im = result.realize(200, 200);
    auto func = [](int x, int y) { return 2*x; };
    if (check_image(im, func)) {
        return -1;
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
    Module m = h.compile_to_module({});
    CheckCalls c;
    m.functions[0].body.accept(&c);

    CallGraphs expected = {
        {h.name(), {g_in_h.name()}},
        {g_in_h.name(), {g.name()}},
        {g.name(), {f.name()}},
        {f.name(), {e_in_f.name()}},
        {e_in_f.name(), {e.name()}},
        {e.name(), {}},
    };
    if (check_call_graphs(c.calls, expected) != 0) {
        return -1;
    }

    Image<int> im = h.realize(200, 200);
    auto func = [](int x, int y) { return x + y; };
    if (check_image(im, func)) {
        return -1;
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
    Module m = h.compile_to_module({});
    CheckCalls c;
    m.functions[0].body.accept(&c);

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
    if (check_call_graphs(c.calls, expected) != 0) {
        return -1;
    }

    Image<int> im = h.realize(200, 200);
    auto func = [](int x, int y) { return 4*(x + y); };
    if (check_image(im, func)) {
        return -1;
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
    Module m = g.compile_to_module({});
    CheckCalls c;
    m.functions[0].body.accept(&c);

    CallGraphs expected = {
        {g.name(), {}},
        {g.update(0).name(), {g.name(), f_in_g.name(), h_wrapper.name()}},
        {f_in_g.name(), {f.name()}},
        {f.name(), {}},
        {h_wrapper.name(), {h.name()}},
        {h.name(), {}},
    };
    if (check_call_graphs(c.calls, expected) != 0) {
        return -1;
    }

    Image<int> im = g.realize(200, 200);
    auto func = [](int x, int y) {
        return ((0 <= x && x <= 99) && (0 <= y && y <= 99) && (x + y + 5 < 50)) ? 15 : 10;
    };
    if (check_image(im, func)) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    printf("Running func wrap test\n");
    if (func_wrap_test() != 0) {
        return -1;
    }

    printf("Running global wrap test\n");
    if (global_wrap_test() != 0) {
        return -1;
    }

    printf("Running update is defined after wrap test\n");
    if (update_defined_after_wrap_test() != 0) {
        return -1;
    }

    printf("Running rdom wrapper test\n");
    if (rdom_wrapper_test() != 0) {
        return -1;
    }

    printf("Running global + custom wrapper test\n");
    if (global_and_custom_wrap_test() != 0) {
        return -1;
    }

    printf("Running wrapper depend on mutated func test\n");
    if (wrapper_depend_on_mutated_func_test() != 0) {
        return -1;
    }

    printf("Running wrapper on wrapper test\n");
    if (wrapper_on_wrapper_test() != 0) {
        return -1;
    }

    printf("Running wrapper on rdom predicate test\n");
    if (wrapper_on_rdom_predicate_test() != 0) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}