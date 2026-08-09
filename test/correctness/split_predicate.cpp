#include "Halide.h"

#include <stdio.h>
#include <vector>

using namespace Halide;
using namespace Halide::Internal;

namespace {

void check(const Expr &pred, std::vector<Expr> &expected) {
    std::vector<Expr> result;
    split_into_ands(pred, result);
    bool is_equal = true;

    if (result.size() != expected.size()) {
        is_equal = false;
    } else {
        for (size_t i = 0; i < expected.size(); ++i) {
            if (!equal(simplify(result[i]), simplify(expected[i]))) {
                is_equal = false;
                break;
            }
        }
    }

    if (!is_equal) {
        std::cout << "Expect predicate " << pred << " to be split into:\n";
        for (const auto &e : expected) {
            std::cout << "  " << e << "\n";
        }
        std::cout << "Got:\n";
        for (const auto &e : result) {
            std::cout << "  " << e << "\n";
        }
        internal_error << "\n";
    }
}

}  // namespace

int main() {
    Expr x = Var("x"), y = Var("y"), z = Var("z"), w = Var("w");

    {
        std::vector<Expr> expected;
        expected.push_back(z < 10);
        check(z < 10, expected);
    }

    {
        std::vector<Expr> expected;
        expected.push_back((x < y) || (x == 10));
        check((x < y) || (x == 10), expected);
    }

    {
        std::vector<Expr> expected;
        expected.push_back(x < y);
        expected.push_back(x == 10);
        check((x < y) && (x == 10), expected);
    }

    {
        std::vector<Expr> expected;
        expected.push_back(x < y);
        expected.push_back(x == 10);
        expected.push_back(y == z);
        check((x < y) && (x == 10) && (y == z), expected);
    }

    {
        std::vector<Expr> expected;
        expected.push_back((w == 1) || ((x == 10) && (y == z)));
        check((w == 1) || ((x == 10) && (y == z)), expected);
    }

    {
        std::vector<Expr> expected;
        expected.push_back(x < y);
        expected.push_back((w == 1) || ((x == 10) && (y == z)));
        check((x < y) && ((w == 1) || ((x == 10) && (y == z))), expected);
    }

    printf("Success!\n");
    return 0;
}
