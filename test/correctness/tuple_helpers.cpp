#include "Halide.h"
#include <stdio.h>
#include <string>
#include <vector>

using namespace Halide;
using namespace Halide::Internal;

namespace {

std::vector<std::string> messages;

void my_print(JITUserContext *user_context, const char *message) {
    messages.push_back(message);
}

bool error_occurred = false;

void my_error(JITUserContext *user_context, const char *message) {
    error_occurred = true;
}

// Counts the For loops in the lowered Stmt.
class CountLoops : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        count++;
        return IRMutator::visit(op);
    }

public:
    int count = 0;
};

// Check that each element of 'wrapped' is a call to the intrinsic
// 'op' whose first argument is the corresponding element of 'orig'.
bool check_wrapped(const Tuple &wrapped, const Tuple &orig, Call::IntrinsicOp op) {
    if (wrapped.size() != orig.size()) {
        printf("Tuple size changed from %d to %d\n", (int)orig.size(), (int)wrapped.size());
        return false;
    }
    for (size_t i = 0; i < wrapped.size(); i++) {
        const Call *c = wrapped[i].as<Call>();
        if (!c || !c->is_intrinsic(op) || !equal(c->args[0], orig[i])) {
            std::cerr << "Element " << i << " was not wrapped as expected: " << wrapped[i] << "\n";
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::Profile) || target.has_feature(Target::Debug)) {
        // Both add extra prints, so counting the number of prints is
        // not useful.
        printf("[SKIP] Test incompatible with profiler and debug runtime.\n");
        return 0;
    }

    Var x("x"), y("y");
    Param<int> p("p"), q("q");

    // likely
    {
        Tuple t(x, y * 2);
        if (!check_wrapped(likely(t), t, Call::likely)) {
            return 1;
        }

        // The likely should trigger loop partitioning, so there
        // should be several loops over x.
        Func f("f");
        f(x) = select(x < 10, Tuple(0, 0), likely(Tuple(x, x + 1)));
        CountLoops counter;
        f.add_custom_lowering_pass(&counter, []() {});
        Realization result = f.realize({20});
        if (counter.count < 2) {
            printf("likely on a Tuple did not trigger loop partitioning\n");
            return 1;
        }
        Buffer<int> a = result[0], b = result[1];
        for (int i = 0; i < 20; i++) {
            int correct_a = i < 10 ? 0 : i;
            int correct_b = i < 10 ? 0 : i + 1;
            if (a(i) != correct_a || b(i) != correct_b) {
                printf("result(%d) = (%d, %d) instead of (%d, %d)\n",
                       i, a(i), b(i), correct_a, correct_b);
                return 1;
            }
        }
    }

    // likely_if_innermost
    {
        Tuple t(x, y * 2);
        if (!check_wrapped(likely_if_innermost(t), t, Call::likely_if_innermost)) {
            return 1;
        }
    }

    // strict_float
    {
        Expr a = cast<float>(x), b = cast<float>(y);
        Tuple t(a + b, a * b, x + y);
        Tuple s = strict_float(t);
        for (size_t i = 0; i < 2; i++) {
            const Call *c = s[i].as<Call>();
            if (!c || !c->is_strict_float_intrinsic()) {
                std::cerr << "strict_float did not strictify element " << i << ": " << s[i] << "\n";
                return 1;
            }
        }
        if (!equal(s[2], t[2])) {
            std::cerr << "strict_float changed an integer element: " << s[2] << "\n";
            return 1;
        }
    }

    // memoize_tag
    {
        Tuple t(x, y * 2);
        Tuple m = memoize_tag(t, p, q);
        if (!check_wrapped(m, t, Call::memoize_expr)) {
            return 1;
        }
        for (size_t i = 0; i < m.size(); i++) {
            const Call *c = m[i].as<Call>();
            if (c->args.size() != 3 || !equal(c->args[1], p) || !equal(c->args[2], q)) {
                std::cerr << "memoize_tag did not attach the cache key values: " << m[i] << "\n";
                return 1;
            }
        }
    }

    // require
    {
        Tuple t(x, y * 2);
        Tuple r = require(p > 0, t, "p was", p);
        // The value is the second argument of the require intrinsic.
        for (size_t i = 0; i < r.size(); i++) {
            const Call *c = r[i].as<Call>();
            if (!c || !c->is_intrinsic(Call::require) || !equal(c->args[1], t[i])) {
                std::cerr << "require did not guard element " << i << ": " << r[i] << "\n";
                return 1;
            }
        }

        Func f("f");
        f(x) = require(p > 0, Tuple(x, x * 2), "p was", p);
        f.jit_handlers().custom_error = my_error;

        p.set(1);
        error_occurred = false;
        Realization result = f.realize({10});
        if (error_occurred) {
            printf("There should not have been a requirement error\n");
            return 1;
        }
        Buffer<int> a = result[0], b = result[1];
        for (int i = 0; i < 10; i++) {
            if (a(i) != i || b(i) != i * 2) {
                printf("result(%d) = (%d, %d) instead of (%d, %d)\n",
                       i, a(i), b(i), i, i * 2);
                return 1;
            }
        }

        p.set(0);
        error_occurred = false;
        f.realize(result);
        if (!error_occurred) {
            printf("There should have been a requirement error\n");
            return 1;
        }
    }

    // print
    {
        Tuple t(x, x * 2);
        Tuple pr = print(t, "at", x);
        if (!equal(pr[1], t[1])) {
            std::cerr << "print changed the second element: " << pr[1] << "\n";
            return 1;
        }

        Func f("f");
        f(x) = print(Tuple(x, x * 2), "at", x);
        f.jit_handlers().custom_print = my_print;
        messages.clear();
        Realization result = f.realize({3});
        Buffer<int> a = result[0], b = result[1];
        for (int i = 0; i < 3; i++) {
            if (a(i) != i || b(i) != i * 2) {
                printf("result(%d) = (%d, %d) instead of (%d, %d)\n",
                       i, a(i), b(i), i, i * 2);
                return 1;
            }
        }
        std::vector<std::string> expected = {"0 0 at 0\n", "1 2 at 1\n", "2 4 at 2\n"};
        if (messages != expected) {
            printf("print on a Tuple printed the wrong thing:\n");
            for (const auto &m : messages) {
                printf("  %s", m.c_str());
            }
            return 1;
        }
    }

    // print_when
    {
        Func f("f");
        f(x) = print_when(x == 1, Tuple(x, x * 2), "at", x);
        f.jit_handlers().custom_print = my_print;
        messages.clear();
        Realization result = f.realize({3});
        Buffer<int> a = result[0], b = result[1];
        for (int i = 0; i < 3; i++) {
            if (a(i) != i || b(i) != i * 2) {
                printf("result(%d) = (%d, %d) instead of (%d, %d)\n",
                       i, a(i), b(i), i, i * 2);
                return 1;
            }
        }
        std::vector<std::string> expected = {"1 2 at 1\n"};
        if (messages != expected) {
            printf("print_when on a Tuple printed the wrong thing:\n");
            for (const auto &m : messages) {
                printf("  %s", m.c_str());
            }
            return 1;
        }
    }

    // FuncRefs convert to both Expr and Tuple, so they get their own
    // overloads, which return Tuples. A one-element Tuple can be used
    // as an Expr, so single-valued Funcs work in Expr contexts.
    {
        Func g("g"), h("h");
        g(x) = x;
        h(x) = Tuple(x, x * 2);

        Expr e = likely(g(x));
        const Call *c = e.as<Call>();
        if (!c || !c->is_intrinsic(Call::likely) || !equal(c->args[0], Expr(g(x)))) {
            std::cerr << "likely on a FuncRef gave " << e << "\n";
            return 1;
        }

        Tuple t = likely(h(x));
        if (!check_wrapped(t, Tuple(h(x)), Call::likely)) {
            return 1;
        }

        // The other helpers should accept FuncRefs without ambiguity,
        // whether or not the Func is single-valued.
        e = likely_if_innermost(g(x));
        e = strict_float(g(x));
        e = memoize_tag(g(x), p);
        e = require(p > 0, g(x), "p was", p);
        e = print(g(x), "at", x);
        e = print_when(x == 1, g(x), "at", x);
        e = select(x < 10, g(x), g(x + 1));
        e = likely(g(x)) + 1;
        t = likely_if_innermost(h(x));
        t = strict_float(h(x));
        t = memoize_tag(h(x), p);
        t = require(p > 0, h(x), "p was", p);
        t = print(h(x), "at", x);
        t = print_when(x == 1, h(x), "at", x);
        t = select(x < 10, h(x), h(x + 1));
    }

    // Tuples can be iterated over.
    {
        Tuple t(x, y * 2);
        for (Expr &e : t) {
            e = likely(e);
        }
        if (!check_wrapped(t, Tuple(x, y * 2), Call::likely)) {
            return 1;
        }
    }

    // One-element Tuples convert to Exprs.
    {
        Expr e = Tuple(x + 1);
        if (!equal(e, x + 1)) {
            std::cerr << "Converting a one-element Tuple to an Expr gave " << e << "\n";
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
