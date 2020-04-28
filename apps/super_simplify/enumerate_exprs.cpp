#include "Halide.h"

using namespace Halide;

using std::vector;
using std::pair;
using std::set;

void enumerate_exprs(int size, vector<pair<int, Expr>> *result) {
    if (size < 0) {
        return;
    }

    enumerate_exprs(size - 1, result);
    if (size == 0) {
        result->emplace_back(pair<int, Expr>{0, Var("x")});
        result->emplace_back(pair<int, Expr>{0, Var("y")});
        result->emplace_back(pair<int, Expr>{0, Var("z")});
        result->emplace_back(pair<int, Expr>{0, Var("w")});
        result->emplace_back(pair<int, Expr>{0, Halide::Internal::Variable::make(Bool(), "c")});
        result->emplace_back(pair<int, Expr>{1, -2});
        result->emplace_back(pair<int, Expr>{1, -1});
        result->emplace_back(pair<int, Expr>{1, 0});
        result->emplace_back(pair<int, Expr>{1, 1});
        result->emplace_back(pair<int, Expr>{1, 2});
    } else {
        size_t rs = result->size();
        for (size_t i = 0; i < rs; i++) {
            for (size_t j = 0; j < rs; j++) {
                const auto a = (*result)[i];
                const auto b = (*result)[j];
                if (a.first + b.first < size) {
                    if (a.second.type().is_int() && b.second.type().is_int()) {
                        result->emplace_back(pair<int, Expr>{a.first + b.first + 1, a.second + b.second});
                        result->emplace_back(pair<int, Expr>{a.first + b.first + 1, a.second - b.second});
                        result->emplace_back(pair<int, Expr>{a.first + b.first + 1, a.second * b.second});
                        if (is_const(b.second) && !is_zero(b.second)) {
                            result->emplace_back(pair<int, Expr>{a.first + b.first + 1, a.second / b.second});
                            result->emplace_back(pair<int, Expr>{a.first + b.first + 1, a.second % b.second});
                        }
                        result->emplace_back(pair<int, Expr>{a.first + b.first + 1, min(a.second, b.second)});
                        result->emplace_back(pair<int, Expr>{a.first + b.first + 1, max(a.second, b.second)});
                        result->emplace_back(pair<int, Expr>{a.first + b.first + 1, a.second == b.second});
                        result->emplace_back(pair<int, Expr>{a.first + b.first + 1, a.second != b.second});
                        result->emplace_back(pair<int, Expr>{a.first + b.first + 1, a.second < b.second});
                    } else if (a.second.type().is_bool() && b.second.type().is_bool()) {
                        result->emplace_back(pair<int, Expr>{a.first + b.first + 1, a.second || b.second});
                        result->emplace_back(pair<int, Expr>{a.first + b.first + 1, a.second && b.second});
                    }
                    for (size_t k = 0; k < rs; k++) {
                        const auto c = (*result)[k];
                        if (a.first + b.first + c.first < size &&
                            a.second.type().is_bool() &&
                            b.second.type() == c.second.type()) {
                            result->emplace_back(pair<int, Expr>{a.first + b.first + c.first + 1,
                                                                     select(a.second, b.second, c.second)});
                        }
                    }
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    int size = 3;
    if (argc > 1) {
        size = std::atoi(argv[1]);
    }

    set<Expr, Halide::Internal::IRDeepCompare> result_set;
    vector<pair<int, Expr>> result;
    enumerate_exprs(size, &result);
    std::cerr << "Generated " << result.size() << " unsimplified expressions...\n";
    for (const auto &e : result) {
        result_set.insert(simplify(e.second));
    }

    for (const Expr &e : result_set) {
        std::cout << e << "\n";
    }


    return 0;
}
