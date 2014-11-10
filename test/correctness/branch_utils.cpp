#include <stdio.h>

#include <Halide.h>

#include <algorithm>
#include <iostream>

using namespace Halide;
using namespace Halide::Internal;

bool test_branches_in_var() {
    Scope<Expr> scope;

    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");

    // Test basic functionality of branches_in_var
    Stmt s1 = IfThenElse::make(x < 0, Evaluate::make(0));
    Expr e1 = Select::make(x < 0, 0, x);
    Expr e2 = clamp(x, 0, 100);

    if (!branches_in_var(s1, "x", scope)) {
        return false;
    }

    if (!branches_in_var(e1, "x", scope)) {
        return false;
    }

    if (branches_in_var(e2, "x", scope)) {
        return false;
    }

    if (!branches_in_var(e2, "x", scope, true)) {
        return false;
    }

    // Test branches_in_var uses scope correctly.
    Stmt s2 = IfThenElse::make(y < 0, Evaluate::make(0));
    Expr e3 = Select::make(y < 0, 0, x);

    scope.push("y", 4*x);

    if (!branches_in_var(s2, "x", scope)) {
        return false;
    }

    if (!branches_in_var(e3, "x", scope)) {
        return false;
    }

    // Test branches_in_var doesn't explode with deeply nested scopes.
    constexpr int N = 100;

    Expr v[N];
    for (int i = 0; i < N; ++i) {
        std::string vi = "v" + int_to_string(i);
        v[i] = Variable::make(Int(32), vi);
        if (i == 0) {
            scope.push(vi, x*x);
        } else {
            scope.push(vi, v[i-1]*v[i-1]);
        }
    }

    Expr vN = v[N-1];
    Stmt s3 = IfThenElse::make(vN < 0, Evaluate::make(0));

    if (!branches_in_var(s3, "x", scope)) {
        return false;
    }
}


int main() {
    if (!test_branches_in_var()) {
        std::cout << "Failure.\n";
        return -1;
    }

    std::cout << "Success!\n";
    return 0;
}
