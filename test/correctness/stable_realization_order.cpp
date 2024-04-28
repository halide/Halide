#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    // Verify that the realization order is invariant to anything to do with
    // unique_name counters.

    std::vector<std::string> expected;

    for (int i = 0; i < 10; i++) {
        std::map<std::string, Function> env;
        Var x, y;
        Expr s = 0;
        std::vector<Func> funcs(8);
        for (size_t i = 0; i < funcs.size() - 1; i++) {
            funcs[i](x, y) = x + y;
            s += funcs[i](x, y);
            env[funcs[i].name()] = funcs[i].function();
        }
        funcs.back()(x, y) = s;
        env[funcs.back().name()] = funcs.back().function();

        auto r = realization_order({funcs.back().function()}, env).first;
        // Ties in the realization order are supposed to be broken by any
        // alphabetical prefix of the Func name followed by time of
        // definition. All the Funcs in this test have the same name, so it
        // should just depend on time of definition.
        assert(r.size() == funcs.size());
        for (size_t i = 0; i < funcs.size(); i++) {
            if (funcs[i].name() != r[i]) {
                debug(0) << "Unexpected realization order: "
                         << funcs[i].name() << " != " << r[i] << "\n";
            }
        }
    }

    printf("Success!\n");
    return 0;
}
