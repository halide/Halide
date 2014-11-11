#include <stdio.h>

#include <Halide.h>

#include <algorithm>
#include <iostream>

using namespace Halide;
using namespace Halide::Internal;

Scope<Expr> scope;

Expr x = Variable::make(Int(32), "x");
Expr y = Variable::make(Int(32), "y");

constexpr int N = 100;
Expr v[N];

void fill_scope() {
    scope.push("y", 4*x);

    for (int i = 0; i < N; ++i) {
        std::string vi = "v" + int_to_string(i);
        v[i] = Variable::make(Int(32), vi);
        if (i == 0) {
            scope.push(vi, x*x);
        } else {
            scope.push(vi, v[i-1]*v[i-1]);
        }
    }
}

bool test_branches_in_var() {
    // Test basic functionality of branches_in_var
    Stmt s1 = IfThenElse::make(x < 0, Evaluate::make(0));
    Expr e1 = Select::make(x < 0, 0, x);
    Expr e2 = clamp(x, 0, 100);

    if (!branches_in_var(s1, "x", scope)) {
        std::cout << "Expected to branch in x:\n" << s1;
        return false;
    }

    if (!branches_in_var(e1, "x", scope)) {
        std::cout << "Expected to branch in x: " << e1 << "\n";
        return false;
    }

    if (branches_in_var(e2, "x", scope)) {
        std::cout << "Expected not to branch in x: " << e2 << "\n";
        return false;
    }

    if (!branches_in_var(e2, "x", scope, true)) {
        std::cout << "Expected to branch in x: " << e2 << "\n";
        return false;
    }

    // Test branches_in_var uses scope correctly.
    Stmt s2 = IfThenElse::make(y < 0, Evaluate::make(0));
    Expr e3 = Select::make(y < 0, 0, x);

    if (!branches_in_var(s2, "x", scope)) {
        std::cout << "Expected to branch in x:\n" << s2;
        return false;
    }

    if (!branches_in_var(e3, "x", scope)) {
        std::cout << "Expected to branch in x: " << e3 << "\n";
        return false;
    }

    // Test branches_in_var doesn't explode with deeply nested scopes.
    Expr vN = v[N-1];
    Stmt s3 = IfThenElse::make(vN < 0, Evaluate::make(0));

    if (!branches_in_var(s3, "x", scope)) {
        std::cout << "Expected to branch in x:\n" << s3;
        return false;
    }

    return true;
}

bool test_normalize_prune_branches() {
    Scope<Interval> bounds;
    Scope<int> free_vars;
    free_vars.push("x", 0);

    Stmt then_case = Evaluate::make(0);
    Stmt else_case = Evaluate::make(1);

    Stmt s1 = IfThenElse::make(x != 0 && x != 1, then_case, else_case);
    Stmt s1_ans1 = IfThenElse::make(x < 1, then_case, IfThenElse::make(1 < x, then_case, else_case));
    Stmt s1_ans = IfThenElse::make(x < 0, s1_ans1, IfThenElse::make(0 < x, s1_ans1, else_case));
    Stmt s1_nrm = normalize_branch_conditions(s1, scope);
    Stmt s1_prune = prune_branches(s1_nrm, "x", scope, bounds, free_vars);
    Stmt s1_ans_prune = IfThenElse::make(
        x < 0, then_case, IfThenElse::make(
            0 < x, IfThenElse::make(1 < x, then_case, else_case), else_case));

    if (!equal(s1_nrm, s1_ans)) {
        std::cout << "Normalized:\n" << s1_nrm
                  << "Expected:\n" << s1_ans;
        return false;
    }

    if (!equal(s1_prune, s1_ans_prune)) {
        std::cout << "Pruned:\n" << s1_prune
                  << "Expected:\n" << s1_ans_prune;
        return false;
    }

    Expr e1 = select(x != 0 && x != 1, 0, 1);
    Expr e1_ans1 = select(x < 1, 0, select(1 < x, 0, 1));
    Expr e1_ans = select(x < 0, e1_ans1, select(0 < x, e1_ans1, 1));
    Expr e1_nrm = normalize_branch_conditions(e1, scope);
    Expr e1_prune = prune_branches(e1_nrm, "x", scope, bounds, free_vars);
    Expr e1_ans_prune = select(x < 0, 0, select(0 < x, select(1 < x, 0, 1), 1));

    if (!equal(e1_nrm, e1_ans)) {
        std::cout << "Normalized: " << e1_nrm << "\n"
                  << "Expected: " << e1_ans << "\n";
        return false;
    }

    if (!equal(e1_prune, e1_ans_prune)) {
        std::cout << "Pruned: " << e1_prune << "\n"
                  << "Expected: " << e1_ans_prune << "\n";
        return false;
    }

    return true;
}

int main() {
    fill_scope();

    if (!test_branches_in_var()) {
        std::cout << "Failure.\n";
        return -1;
    }

    if (!test_normalize_prune_branches()) {
        std::cout << "Failure.\n";
        return -1;
    }

    std::cout << "Success!\n";
    return 0;
}
