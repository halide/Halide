#include <stdio.h>

#include <Halide.h>

#include <algorithm>
#include <iostream>

using namespace Halide;

void test_collect_linear_terms() {
    Var x("x"), y("y"), z("z");

    /* Simplify an expression by collecting linear terms:
     *
     *    3(x - y + (z - 1)/15) + (x/2) - (y + 3*z)/6
     *
     * Should simplify to:
     *
     *    (3 + 1/2)x + (-3 - 1/6)y + (3/15 - 3/6)z + (-3/15)
     *    = 3.5*x - 3.16667*y - 0.3*z - 0.2
     */
    Expr e1 = 3.f*(x - y + (z - 1.f) / 15.f) + (x / 2.f) - (y + 3.f*z) / 6.f;
    Expr e2 = 10.f*z - (2.f*x + y)/3.f + 10.f*y;

    std::vector<Internal::Term> terms;
    collect_linear_terms(e1, terms);

    std::cout << "original expression: " << e1 << "\ncollected into terms:\n";
    if (terms[0].coeff.defined()) {
        std::cout << "\t" << terms[0].coeff << "\n";
    } else {
        std::cout << "\t0\n";
    }

    for (int i = 1; i< terms.size(); ++i) {
        std::cout << "\t" << terms[i].coeff << " * "
                  << terms[i].var->name << std::endl;
    }

    terms.clear();
    collect_linear_terms(e2, terms);

    std::cout << "original expression: " << e2 << "\ncollected into terms:\n";
    if (terms[0].coeff.defined()) {
        std::cout << "\t" << terms[0].coeff << "\n";
    } else {
        std::cout << "\t0\n";
    }

    for (int i = 1; i< terms.size(); ++i) {
        std::cout << "\t" << terms[i].coeff << " * "
                  << terms[i].var->name << std::endl;
    }
}

void test_linear_solve() {
    Var x("x"), y("y"), z("z");

    Expr e1 = 3.f*(x - y + (z - 1.f) / 15.f) + (x / 2.f) - (y + 3.f*z) / 6.f;
    Expr e2 = 10.f*z - (2.f*x + y)/3.f + 10.f*y;
    Expr eq = (e1 == e2);

    std::cout << "solving for " << x.name() << " in: " << eq << ".\n";
    Expr ans = solve_for_linear_variable(eq, x);
    std::cout << "solution: " << ans << ".\n";
}

int main() {
    test_collect_linear_terms();
    test_linear_solve();
    return 0;
}
