#include "Halide.h"
#include "merge_rules.h"
#include "single_rule.h"

#include <iostream>
#include <memory>
using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y"), z("z"), w("w"), u("u"), c0("c0"), c1("c1"), c2("c2");
    std::vector<RewriteRule> rules = {
    //   {x - (y + x), -y},
    //   {x - (x + y), -y},
    //   {(x - y) - (z + x), -(y + z)},
    {(x + y) - (y + x), 0},
    { (x + y) - (x + y), 0},
    // {(x + y) - (z + (y + x)), -z},
    // {(x + y) - (z + (y + w)), x - (z + w)},
    };
    
    std::string func = merge_rules_function(rules, "simplify_add_example", "expr");
    std::string func2 = construct_simplifier_function(rules, "simplify_add_example", "expr");
    
    std::cout << func << "\n";
    std::cout << func2 << "\n";
    return 0;
}



