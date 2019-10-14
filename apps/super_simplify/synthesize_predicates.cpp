// Run predicate synthesis as a standalone utility

#include "parser.h"
#include "synthesize_predicate.h"
#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;
using std::vector;
using std::map;
using std::string;

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: ./synthesize_predicates halide_exprs.txt\n";
        return 0;
    }

    vector<Expr> exprs = parse_halide_exprs_from_file(argv[1]);
    vector<Expr> predicates;

    for (auto e : exprs) {
        const EQ *eq = e.as<EQ>();
        if (!eq) {
            std::cerr << "All expressions must be equalities: " << e << "\n";
            return 1;
        }
        map<string, Expr> binding;
        Expr predicate = synthesize_predicate(eq->a, eq->b, vector<map<string, Expr>>{}, &binding);
        predicates.push_back(predicate);
    }

    for (size_t i = 0; i < exprs.size(); i++) {
        std::cout << predicates[i] << " implies " << exprs[i] << "\n";
    }

    return 0;
};
