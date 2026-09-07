#include "Halide.h"
#include "parser.h"
#include "super_simplify.h"

using namespace Halide;

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cout << "Usage: ./super_simplify halide_exprs.txt max_size\n";
        return 0;
    }

    std::vector<Expr> exprs = parse_halide_exprs_from_file(argv[1]);
    const int max_size = std::atoi(argv[2]);

    for (const auto &e : exprs) {
        Expr simpler = super_simplify(e, max_size);
        std::cout << e << " -> " << simpler << "\n";
    }

    return 0;
}
