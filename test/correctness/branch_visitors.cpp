#include <stdio.h>

#include <Halide.h>

#include <algorithm>
#include <iostream>

using namespace Halide;
using namespace Halide::Internal;

Scope<Expr> scope;
Scope<int> linearity;

Expr x = Variable::make(Int(32), "x");
Expr y = Variable::make(Int(32), "y");
Expr z = Variable::make(Int(32), "z");

const int N = 100;
Expr v[N];

void fill_scope() {
    // e.g. let y = 4*x
    scope.push("y", 4*x);
    linearity.push("y", Linearity::Linear);

    for (int i = 0; i < N; ++i) {
        // e.g. let x0 = x*x
        //      let x1 = x0*x0
        //      let x2 = x1*x1
        //      ...
        std::string vi = "v" + int_to_string(i);
        v[i] = Variable::make(Int(32), vi);
        if (i == 0) {
            scope.push(vi, x*x);
        } else {
            scope.push(vi, v[i-1]*v[i-1]);
        }
        linearity.push(vi, Linearity::NonLinear);
    }
}

bool test_branches_in_var() {
    Stmt do_foo = Evaluate::make(Call::make(Int(32), "foo", std::vector<Expr>(), Call::Extern));

    // Test basic functionality of branches_in_var
    Stmt s1 = IfThenElse::make(x < 0, do_foo);
    Expr e1 = Select::make(x < 0, 0, x);
    Expr e2 = clamp(x, 0, 100);

    if (!branches_linearly_in_var(s1, "x", linearity)) {
        std::cout << "Expected to branch in x:\n" << s1;
        return false;
    }

    if (!branches_linearly_in_var(e1, "x", linearity)) {
        std::cout << "Expected to branch in x: " << e1 << "\n";
        return false;
    }

    if (branches_linearly_in_var(e2, "x", linearity)) {
        std::cout << "Expected not to branch in x: " << e2 << "\n";
        return false;
    }

    if (!branches_linearly_in_var(e2, "x", linearity, true)) {
        std::cout << "Expected to branch in x: " << e2 << "\n";
        return false;
    }

    // Test branches_in_var uses linearity correctly.
    Stmt s2 = IfThenElse::make(y < 0, do_foo);
    Expr e3 = Select::make(y < 0, 0, x);

    if (!branches_linearly_in_var(s2, "x", linearity)) {
        std::cout << "Expected to branch in x:\n" << s2;
        return false;
    }

    if (!branches_linearly_in_var(e3, "x", linearity)) {
        std::cout << "Expected to branch in x: " << e3 << "\n";
        return false;
    }

    // Test branches_in_var doesn't explode with deeply nested linearitys.
    Expr vN = v[N-1];
    Stmt s3 = IfThenElse::make(vN < 0, do_foo);

    if (branches_linearly_in_var(s3, "x", linearity)) {
        std::cout << "Expected not to branch in x:\n" << s3;
        return false;
    }

    Stmt s4 = LetStmt::make("z", e3, For::make("w", 0, 10, For::Serial, Store::make("s", 0, z)));

    if (!branches_linearly_in_var(s4, "x", linearity)) {
        std::cout << "Expected to branch in x: " << s4 << "\n";
        return false;
    }

    return true;
}

bool test_normalize_branches() {
    Scope<Interval> bounds;
    Scope<int> free_vars;
    free_vars.push("x", 0);

    Stmt then_case = Evaluate::make(Call::make(Int(32), "foo", std::vector<Expr>(), Call::Extern));
    Stmt else_case = Evaluate::make(Call::make(Int(32), "bar", std::vector<Expr>(), Call::Extern));

    Stmt s1 = IfThenElse::make(x != 0 && x != 1, then_case, else_case);
    Stmt s1_ans1 = IfThenElse::make(x < 1, then_case, IfThenElse::make(1 < x, then_case, else_case));
    Stmt s1_nrm = normalize_branch_conditions(s1, "x", scope, bounds, free_vars);
    Stmt s1_ans = IfThenElse::make(
        x < 0, then_case, IfThenElse::make(
            0 < x, IfThenElse::make(1 < x, then_case, else_case), else_case));

    if (!equal(s1_nrm, s1_ans)) {
        std::cout << "Normalized:\n" << s1_nrm
                  << "Expected:\n" << s1_ans;
        return false;
    }

    Expr e1 = select(x != 0 && x != 1, 0, 1);
    Expr e1_ans1 = select(x < 1, 0, select(1 < x, 0, 1));
    Expr e1_nrm = normalize_branch_conditions(e1, "x", scope, bounds, free_vars);
    Expr e1_ans = select(x < 0, 0, select(0 < x, select(1 < x, 0, 1), 1));

    if (!equal(e1_nrm, e1_ans)) {
        std::cout << "Normalized: " << e1_nrm << "\n"
                  << "Expected: " << e1_ans << "\n";
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

    if (!test_normalize_branches()) {
        std::cout << "Failure.\n";
        return -1;
    }

    std::cout << "Success!\n";
    return 0;
}
