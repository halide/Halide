#include "Halide.h"

#include <stdio.h>
#include <map>

namespace {

using std::map;
using std::pair;
using std::string;
using std::vector;

using namespace Halide;
using namespace Halide::Internal;

class CollectPrefetches : public IRVisitor {
private:
    using IRVisitor::visit;

    void visit(const Call *op) {
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
            if (!equal(expected[i][j], result[i][j])) {
                std::cout << "Expect \"" << expected[i][j] << "\", got \""
                          << result[i][j] << " instead\n";
                return false;
            }
        }
    }
    return true;
}

int get_max_byte_size(const Target &t) {
    // See \ref reduce_prefetch_dimension for max_byte_size
    return (t.arch == Target::ARM) ? 32 : 64;
}

int get_stride(const Target &t, int elem_byte_size) {
    return get_max_byte_size(t) / elem_byte_size;
}

int test1(const Target &t) {
    Func f("f"), g("g");
    Var x("x");

    f(x) = x;
    g(x) = f(0) + f(1);

    f.compute_root();
    g.prefetch(f, x, 8);

    Module m = g.compile_to_module({});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()) , 0, 1, get_stride(t, 4)}};
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
    g(x) = f(0) + f(1);

    f.compute_root();
    g.specialize(p).prefetch(f, x, 8);
    g.specialize_fail("No prefetch");

    Module m = g.compile_to_module({p});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()) , 0, 1, get_stride(t, 4)}};
    if (!check(expected, collect.prefetches)) {
        return -1;
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();

    printf("Running prefetch test1\n");
    if (test1(t) != 0) {
        return -1;
    }
    printf("Running prefetch test2\n");
    if (test2(t) != 0) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
