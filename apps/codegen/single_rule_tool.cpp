#include "Halide.h"
#include "single_rule.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y"), z("z"), w("w"), u("u"), c0("c0"), c1("c1"), c2("c2");
    std::vector<RewriteRule> rules = {
        {x + x, x * 2},
        {(x - y) + y, x},
        {x + (y - x), y},
        {min(x, y + c0) + c1, min(x + c1, y), c0 + c1 == 0},
    };
    std::string func = construct_simplifier_function(rules, "simplify_add_example", "expr");
    
    std::cout << func << "\n";
    return 0;
}
