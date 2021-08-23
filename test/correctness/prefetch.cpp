#include "Halide.h"

#include <map>
#include <stdio.h>

namespace {

using std::vector;

using namespace Halide;
using namespace Halide::Internal;

template<typename T>
Expr wild() {
    return Variable::make(halide_type_of<T>(), "*");
}

class CollectPrefetches : public IRVisitor {
private:
    using IRVisitor::visit;

    void visit(const Call *op) override {
        if (op->is_intrinsic(Call::prefetch)) {
            prefetches.push_back(op->args);
        }
    }

public:
    vector<vector<Expr>> prefetches;
};

bool check(const vector<vector<Expr>> &expected, vector<vector<Expr>> &result) {
    if (result.size() != expected.size()) {
        std::cout << "Expect " << expected.size() << " prefetches instead of "
                  << result.size() << "\n";
        return false;
    }
    for (size_t i = 0; i < expected.size(); ++i) {
        if (expected[i].size() != result[i].size()) {
            std::cout << "Expect prefetch args of size " << expected[i].size()
                      << ", got " << result[i].size() << " instead\n";
            return false;
        }
        for (size_t j = 0; j < expected[i].size(); ++j) {
            const Variable *var = expected[i][j].as<Variable>();
            bool is_wild = var && var->name == "*";
            if (!is_wild && !equal(expected[i][j], result[i][j])) {
                std::cout << "Expect \"" << expected[i][j] << "\" at arg index "
                          << j << ", got \"" << result[i][j] << " instead\n";
                return false;
            }
        }
    }
    return true;
}

Expr get_max_byte_size(const Target &t) {
    // See \ref reduce_prefetch_dimension for max_byte_size
    Expr max_byte_size;
    if (t.has_feature(Target::HVX)) {
        max_byte_size = Expr();
    } else if (t.arch == Target::ARM) {
        max_byte_size = 32;
    } else {
        max_byte_size = 64;
    }
    return max_byte_size;
}

Expr get_stride(const Target &t, const Expr &elem_byte_size) {
    Expr max_byte_size = get_max_byte_size(t);
    return max_byte_size.defined() ? simplify(max_byte_size / elem_byte_size) : 1;
}

int test1(const Target &t) {
    Func f("f"), g("g");
    Var x("x");

    f(x) = x;
    g(x) = f(0);

    f.compute_root();
    g.prefetch(f, x, x, 8);

    Module m = g.compile_to_module({});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()), 0, 1, get_stride(t, 4)}};
    if (!check(expected, collect.prefetches)) {
        return -1;
    }
    return 0;
}

int test2(const Target &t) {
    Param<bool> p;

    Func f("f"), g("g");
    Var x("x");

    f(x) = x;
    g(x) = f(0);

    f.compute_root();
    g.specialize(p).prefetch(f, x, x, 8);
    g.specialize_fail("No prefetch");

    Module m = g.compile_to_module({p});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()), 0, 1, get_stride(t, 4)}};
    if (!check(expected, collect.prefetches)) {
        return -1;
    }
    return 0;
}

int test3(const Target &t) {
    Func f("f"), g("g"), h("h");
    Var x("x"), xo("xo");

    f(x) = x;
    h(x) = f(x) + 1;
    g(x) = h(0);

    f.compute_root();
    g.split(x, xo, x, 32);
    h.compute_at(g, xo);
    g.prefetch(f, xo, xo, 1);

    Module m = g.compile_to_module({});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()), 0, 1, get_stride(t, 4)}};
    if (!check(expected, collect.prefetches)) {
        return -1;
    }
    return 0;
}

int test4(const Target &t) {
    Func f("f"), g("g"), h("h");
    Var x("x");

    f(x) = x;
    h(x) = f(x) + 1;
    g(x) = h(0);

    f.compute_root();
    h.compute_root();
    g.prefetch(f, x, x, 1);

    Module m = g.compile_to_module({});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    // There shouldn't be any prefetches since there is no call to 'f'
    // within the loop nest of 'g'
    vector<vector<Expr>> expected = {};
    if (!check(expected, collect.prefetches)) {
        return -1;
    }
    return 0;
}

int test5(const Target &t) {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(0, 0);

    f.compute_root();
    g.prefetch(f, x, y, 8);

    Module m = g.compile_to_module({});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()), 0, 1, get_stride(t, 4)}};
    if (!check(expected, collect.prefetches)) {
        return -1;
    }
    return 0;
}

int test6(const Target &t) {
    Param<bool> p;

    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    g(x, y) = f(0, 0);

    f.compute_root();
    g.specialize(p).prefetch(f, x, y, 8);
    g.specialize_fail("No prefetch");

    Module m = g.compile_to_module({p});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()), 0, 1, get_stride(t, 4)}};
    if (!check(expected, collect.prefetches)) {
        return -1;
    }
    return 0;
}

int test7(const Target &t) {
    Func f("f"), g("g"), h("h");
    Var x("x"), xo("xo"), y("y");

    f(x, y) = x + y;
    h(x, y) = f(x, y) + 1;
    g(x, y) = h(0, 0);

    f.compute_root();
    g.split(x, xo, x, 32);
    h.compute_at(g, xo);
    g.prefetch(f, xo, y, 1);

    Module m = g.compile_to_module({});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()), 0, 1, get_stride(t, 4)}};
    if (!check(expected, collect.prefetches)) {
        return -1;
    }
    return 0;
}

int test8(const Target &t) {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");

    f(x, y) = x + y;
    h(x, y) = f(x, y) + 1;
    g(x, y) = h(0, 0);

    f.compute_root();
    h.compute_root();
    g.prefetch(f, x, y, 1);

    Module m = g.compile_to_module({});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    // There shouldn't be any prefetches since there is no call to 'f'
    // within the loop nest of 'g'
    vector<vector<Expr>> expected = {};
    if (!check(expected, collect.prefetches)) {
        return -1;
    }
    return 0;
}

int test9(const Target &t) {
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    f.compute_root();

    g(x, y) = f(x, y);
    g.tile(x, y, xo, yo, xi, yi, 8, 4, TailStrategy::RoundUp)
        .vectorize(xi)
        .unroll(yi);

    f.in()
        .compute_at(g, xo)
        .vectorize(x)
        .unroll(y)
        .prefetch(f, x, y, 123, PrefetchBoundStrategy::NonFaulting);

    Module m = g.compile_to_module({});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected;
    for (int i = 0; i < 4; i++) {
        // The offset arg is a variable that is ticklish to get right, so just use a wildcard for matching
        expected.push_back({Variable::make(Handle(), f.name()), wild<int>(), 1, get_stride(t, 4)});
    }
    if (!check(expected, collect.prefetches)) {
        return -1;
    }
    return 0;
}

int test10(const Target &t) {
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    f.compute_root();

    g(x, y) = f(x, y);
    g.tile(x, y, xo, yo, xi, yi, 8, 4, TailStrategy::RoundUp)
        .vectorize(xi)
        .unroll(yi);

    f.in()
        .compute_at(g, xo)
        .split(x, xo, xi, 4)
        .vectorize(xi)
        .unroll(xo)
        .reorder(xi, y, xo)
        .unroll(y)
        .prefetch(f, y, xo, 123 / 4, PrefetchBoundStrategy::NonFaulting);

    Module m = g.compile_to_module({});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected;
    for (int i = 0; i < 8; i++) {
        // The offset arg is a variable that is ticklish to get right, so just use a wildcard for matching
        expected.push_back({Variable::make(Handle(), f.name()), wild<int>(), 1, get_stride(t, 4)});
    }
    if (!check(expected, collect.prefetches)) {
        return -1;
    }
    return 0;
}

int test11(const Target &t) {
    Func f("f"), g("g");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    f(x, y) = x + y;
    f.compute_root();

    g(x, y) = f(x, y);
    g.tile(x, y, xo, yo, xi, yi, 8, 4, TailStrategy::RoundUp)
        .vectorize(xi)
        .unroll(yi);

    f.in()
        .compute_at(g, xo)
        .split(x, xo, xi, 4, TailStrategy::RoundUp)
        .vectorize(xi)
        .unroll(xo)
        .reorder(xi, xo, y)
        .unroll(y)
        .prefetch(f, xo, xo, 123 / 4, PrefetchBoundStrategy::NonFaulting);

    Module m = g.compile_to_module({});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected;
    for (int i = 0; i < 8; i++) {
        // The offset arg is a variable that is ticklish to get right, so just use a wildcard for matching
        expected.push_back({Variable::make(Handle(), f.name()), wild<int>(), 1, get_stride(t, 4)});
    }
    if (!check(expected, collect.prefetches)) {
        return -1;
    }
    return 0;
}

}  // anonymous namespace

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();

    using Fn = int (*)(const Target &t);
    std::vector<Fn> tests = {test1, test2, test3, test4, test5, test6, test7, test8, test9, test10, test11};

    for (size_t i = 0; i < tests.size(); i++) {
        printf("Running prefetch test %d\n", (int)i + 1);
        int result = tests[i](t);
        if (result != 0) {
            printf("   prefetch test %d failed!\n", (int)i + 1);
            return result;
        }
    }

    printf("Success!\n");
    return 0;
}
