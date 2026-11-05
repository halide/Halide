#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

// The .used and .loaded conditions of a Func realized inside one branch of an
// if (e.g. a specialization) should not mention the branch condition: they are
// only read inside that branch.
class CheckSkipStagesConditions : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const LetStmt *op) override {
        if ((starts_with(op->name, "g.") || starts_with(op->name, "h.")) &&
            (ends_with(op->name, ".used") || ends_with(op->name, ".loaded"))) {
            num_conditions++;
            if (expr_uses_var(op->value, "c")) {
                std::cerr << "Skip-stages condition mentions the specialization condition: "
                          << op->name << " = " << op->value << "\n";
                exit(1);
            }
        }
        return IRMutator::visit(op);
    }

public:
    int num_conditions = 0;
};

int main(int argc, char **argv) {
    Var x;
    Param<bool> c("c");
    Param<int> n("n");
    Func f("f"), g("g"), h("h"), out("out");
    f(x) = x;
    g(x) = f(x) * 2;
    h(x) = g(x) + g(x + 1);
    out(x) = select(c, 0, select(n > 3, h(x), 1));

    // g and h are realized in the two branches of the !c branch. f is
    // realized outside all of them, and whether it is used depends on whether
    // g is.
    out.specialize(c);
    out.specialize(n > 10);
    f.compute_root();
    h.compute_at(out, x);
    g.compute_at(out, x);

    CheckSkipStagesConditions checker;
    out.add_custom_lowering_pass(&checker, nullptr);
    out.compile_jit();
    if (checker.num_conditions == 0) {
        std::cerr << "Expected some skip-stages conditions\n";
        return 1;
    }

    for (bool cv : {false, true}) {
        for (int nv : {0, 5}) {
            c.set(cv);
            n.set(nv);
            Buffer<int> result = out.realize({16});
            for (int i = 0; i < 16; i++) {
                int correct = cv ? 0 : nv > 3 ? 4 * i + 2 :
                                                1;
                if (result(i) != correct) {
                    std::cerr << "result(" << i << ") = " << result(i)
                              << " instead of " << correct
                              << " for c = " << cv << ", n = " << nv << "\n";
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
