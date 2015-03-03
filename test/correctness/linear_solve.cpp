#include <stdio.h>

#include "Halide.h"

#include <algorithm>
#include <iostream>

using namespace Halide;
using namespace Halide::Internal;

void print_term(const Term &t) {
    if (!t.var) {
        if (t.coeff.defined()) {
            std::cout << t.coeff;
        } else {
            std::cout << 0;
        }
    } else {
        std::cout << t.coeff << " * " << t.var->name;
    }
}

void print_terms(const std::vector<Term> &terms) {
    std::cout << "{";
    for (size_t i = 0; i < terms.size(); ++i) {
        print_term(terms[i]);
        if (i + 1 < terms.size())
            std::cout << ", ";
    }
    std::cout << "}";
}

int find_term(const Variable *v, const std::vector<Term> &terms) {
    for (size_t i = 0; i < terms.size(); ++i) {
        if ((!v && !terms[i].var) ||
            (v && terms[i].var &&
             terms[i].var->name == v->name)) {
            return i;
        }
    }

    return -1;
}

bool check_terms(const std::vector<Term> &expected_terms, const std::vector<Term> &actual_terms) {
    if (expected_terms.size() != actual_terms.size()) {
        std::cout << "Actual " << actual_terms.size() << " linear terms. "
                  << "Expected " << expected_terms.size() << "\n";
        return false;
    }

    std::vector<bool> found_term(expected_terms.size(), false);
    for (size_t i = 0; i < actual_terms.size(); ++i) {
        const Term &t = actual_terms[i];

        int idx = find_term(t.var, expected_terms);
        if (idx == -1) {
            std::cout << "Could not find actual term ";
            print_term(t);
            std::cout << " among expected terms:\n";
            print_terms(expected_terms);
            std::cout << "\n";
            return false;
        } else {
            found_term[idx] = true;
        }
    }

    for (size_t i = 0; i < found_term.size(); ++i) {
        if (!found_term[i]) {
            std::cout << "Could not find expected term ";
            print_term(expected_terms[i]);
            std::cout << " among actual terms:\n";
            print_terms(actual_terms);
            std::cout << "\n";
            return false;
        }
    }

    return true;
}

bool test_collect_linear_terms() {
    Var x("x"), y("y"), z("z");

    Expr x_var = Variable::make(Float(32), "x");
    Expr y_var = Variable::make(Float(32), "y");
    Expr z_var = Variable::make(Float(32), "z");

    Scope<int> free_vars;
    free_vars.push("x", 0);
    free_vars.push("y", 0);
    free_vars.push("z", 0);

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
    Term t1_0 = {-0.2f, NULL};
    Term t1_x = {3.5f, x_var.as<Variable>()};
    Term t1_y = {-3.f - 1.f/6.f, y_var.as<Variable>()};
    Term t1_z = {-0.3f, z_var.as<Variable>()};
    std::vector<Term> e1_terms(4);
    e1_terms[0] = t1_0;
    e1_terms[1] = t1_x;
    e1_terms[2] = t1_y;
    e1_terms[3] = t1_z;

    std::vector<Term> terms;
    collect_linear_terms(e1, terms, free_vars);
    if (!check_terms(e1_terms, terms)) {
        return false;
    }

    /* Simplify an expression by collecting linear terms:
     *
     *    10z - (2x + y)/3 + 10y
     *
     * Should simplify to:
     *
     *    10z - (2/3)x + (10 - 1/3)y
     *    = 10*z - 0.666667x + 9.666667*y
     */
    Expr e2 = 10.f*z - (2.f*x + y)/3.f + 10.f*y;
    Term t2_0 = {Expr(), NULL};
    Term t2_x = {2.f / 3.f, x_var.as<Variable>()};
    Term t2_y = {10.f - 1.f/3.f, y_var.as<Variable>()};
    Term t2_z = {10.f, z_var.as<Variable>()};
    std::vector<Term> e2_terms(4);
    e2_terms[0] = t2_0;
    e2_terms[1] = t2_x;
    e2_terms[2] = t2_y;
    e2_terms[3] = t2_z;

    terms.clear();
    collect_linear_terms(e2, terms, free_vars);
    if (!check_terms(e2_terms, terms)) {
        return false;
    }

    return true;
}

bool test_linear_solve() {
    Var x("x"), y("y"), z("z");

    Scope<int> free_vars;
    free_vars.push("x", 0);
    free_vars.push("y", 0);
    free_vars.push("z", 0);

    /* Solve an equation for a specific variable:
     *
     *    3(x - y + (z - 1)/15) + (x/2) - (y + 3*z)/6
     *    = 10z - (2x + y)/3 + 10y
     *
     * Should simplify to:
     *
     *    x = [(13 - 1/6)y + (10.3)z - (3/15)] / (3 + 1/2 + 2/3)
     */
    Expr e1 = 3.f*(x - y + (z - 1.f) / 15.f) + (x / 2.f) - (y + 3.f*z) / 6.f;
    Expr e2 = 10.f*z - (2.f*x + y)/3.f + 10.f*y;
    Expr eq = (e1 == e2);
    Expr expected = ((13.f - 1.f/6.f)*y + 10.3f*z - 0.2f) / (3.5f + 2.f/3.f);
    Expr ans = solve_for_linear_variable(eq, x, free_vars);
    Expr actual = ans.as<EQ>()->b;

    std::vector<Term> expected_terms;
    std::vector<Term> actual_terms;
    collect_linear_terms(expected, expected_terms, free_vars);
    collect_linear_terms(actual, actual_terms, free_vars);

    if (!check_terms(expected_terms, actual_terms)) {
        std::cout << "Solving linear expression failed!\n"
                  << "Expected solution: " << expected << "\n"
                  << "Actual solution:   " << ans << "\n";
        return false;
    }

    return true;
}

int main() {
    if (!test_collect_linear_terms()) {
        std::cout << "Failure.\n";
        return -1;
    }

    if (!test_linear_solve()) {
        std::cout << "Failure.\n";
        return -1;
    }

    std::cout << "Success!\n";
    return 0;
}
