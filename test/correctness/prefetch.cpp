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

int test1() {
    Func f("f"), g("g");
    Var x("x");

    f(x) = x;
    g(x) = f(0) + f(7);

    f.compute_root();
    g.prefetch(f, x, 8);

    Module m = g.compile_to_module({});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()) , 0, 1, 8}};
    if (collect.prefetches.size() != expected.size()) {
        std::cout << "Expect " << expected.size() << " prefetches instead of "
                  << collect.prefetches.size() << "\n";
        return -1;
    }
    for (size_t i = 0; i < expected.size(); ++i) {
        if (expected[i].size() != collect.prefetches[i].size()) {
            std::cout << "Expect prefetch args of size " << expected[i].size()
                      << ", got " << collect.prefetches[i].size() << " instead\n";
            return -1;
        }
        for (size_t j = 0; j < expected[i].size(); ++j) {
            if (!equal(expected[i][j], collect.prefetches[i][j])) {
                std::cout << "Expect \"" << expected[i][j] << "\", got \""
                          << collect.prefetches[i][j] << " instead\n";
                return -1;
            }
        }
    }
    return 0;
}

int test2() {
    Param<bool> p;

    Func f("f"), g("g");
    Var x("x");

    f(x) = x;
    g(x) = f(0) + f(7);

    f.compute_root();
    g.specialize(p).prefetch(f, x, 8);

    Module m = g.compile_to_module({p});
    CollectPrefetches collect;
    m.functions()[0].body.accept(&collect);

    vector<vector<Expr>> expected = {{Variable::make(Handle(), f.name()) , 0, 1, 8}};
    if (collect.prefetches.size() != expected.size()) {
        std::cout << "Expect " << expected.size() << " prefetches instead of "
                  << collect.prefetches.size() << "\n";
        return -1;
    }
    for (size_t i = 0; i < expected.size(); ++i) {
        if (expected[i].size() != collect.prefetches[i].size()) {
            std::cout << "Expect prefetch args of size " << expected[i].size()
                      << ", got " << collect.prefetches[i].size() << " instead\n";
            return -1;
        }
        for (size_t j = 0; j < expected[i].size(); ++j) {
            if (!equal(expected[i][j], collect.prefetches[i][j])) {
                std::cout << "Expect \"" << expected[i][j] << "\", got \""
                          << collect.prefetches[i][j] << " instead\n";
                return -1;
            }
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    printf("Running prefetch test1\n");
    if (test1() != 0) {
        return -1;
    }
    printf("Running prefetch test2\n");
    if (test2() != 0) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}
