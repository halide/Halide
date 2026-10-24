#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

std::mt19937 rng;

int64_t sample(const ConstantInterval &i) {
    int64_t upper = i.max_defined ? i.max : 1024;
    int64_t lower = i.min_defined ? i.min : -1024;
    return lower + (rng() % (upper - lower + 1));
}

ConstantInterval random_interval() {
    int64_t a = (rng() % 512) - 256;
    int64_t b = (rng() % 512) - 256;
    ConstantInterval result;
    if (rng() & 1) {
        result.max_defined = true;
        result.max = std::max(a, b);
    }
    if (rng() & 1) {
        result.min_defined = true;
        result.min = std::min(a, b);
    }
    return result;
}

void check_constant_integer_bounds(const Expr &e, const ConstantInterval &expected) {
    ConstantInterval actual = constant_integer_bounds(e);
    if (!(actual == expected)) {
        std::cout << "constant_integer_bounds(" << e << ") = " << actual
                  << ", expected " << expected << "\n";
        exit(1);
    }
}

void test_constant_integer_bounds_of_select() {
    Expr x = Variable::make(Int(32), "x");
    Expr u8 = Variable::make(UInt(8), "u8");
    Expr i8 = Variable::make(Int(8), "i8");

    // A select is bounded by the union of its two values.
    check_constant_integer_bounds(select(x > 0, cast<int>(u8), -3),
                                  ConstantInterval(-3, 255));

    // Nested selects.
    check_constant_integer_bounds(select(x > 0, cast<int>(u8),
                                         select(x < -5, -300, cast<int>(i8))),
                                  ConstantInterval(-300, 255));

    // Vector selects.
    Expr c_vec = Variable::make(Bool(8), "c_vec");
    Expr u8_vec = Variable::make(UInt(8, 8), "u8_vec");
    check_constant_integer_bounds(select(c_vec, cast(Int(16, 8), u8_vec), Broadcast::make(cast<int16_t>(-7), 8)),
                                  ConstantInterval(-7, 255));

    // If either value is unbounded, so is the result (up to the bounds of the type).
    check_constant_integer_bounds(select(x > 0, x, 0),
                                  ConstantInterval::bounds_of_type(Int(32)));

    // if_then_else with both values present behaves like a select.
    check_constant_integer_bounds(Call::make(Int(32), Call::if_then_else,
                                             {x > 0, cast<int>(u8), 1000},
                                             Call::PureIntrinsic),
                                  ConstantInterval(0, 1000));
}

int main(int argc, char **argv) {
    test_constant_integer_bounds_of_select();

    for (int i = 0; i < 1000; i++) {
        std::vector<std::pair<ConstantInterval, int64_t>> values;
        for (int j = 0; j < 10; j++) {
            values.emplace_back(random_interval(), 0);
            values.back().second = sample(values.back().first);
        }

        for (int j = 0; j < 1000; j++) {
            auto a = values[rng() % values.size()];
            auto b = values[rng() % values.size()];
            decltype(a) c;

            auto check = [&](const char *op) {
                if (!c.first.contains(c.second)) {
                    std::cout << "Error for operator " << op << ":\n"
                              << "a: " << a.second << " in " << a.first << "\n"
                              << "b: " << b.second << " in " << b.first << "\n"
                              << "c: " << c.second << " not in " << c.first << "\n";
                    exit(1);
                }
            };

            auto check_scalar = [&](const char *op) {
                if (!c.first.contains(c.second)) {
                    std::cout << "Error for operator " << op << ":\n"
                              << "a: " << a.second << " in " << a.first << "\n"
                              << "b: " << b.second << "\n"
                              << "c: " << c.second << " not in " << c.first << "\n";
                    exit(1);
                }
            };

            // Arithmetic
            if (!add_would_overflow(64, a.second, b.second)) {
                c.first = a.first + b.first;
                c.second = a.second + b.second;
                check("+");
            }

            if (!sub_would_overflow(64, a.second, b.second)) {
                c.first = a.first - b.first;
                c.second = a.second - b.second;
                check("-");
            }

            if (!mul_would_overflow(64, a.second, b.second)) {
                c.first = a.first * b.first;
                c.second = a.second * b.second;
                check("*");
            }

            c.first = a.first / b.first;
            c.second = div_imp(a.second, b.second);
            check("/");

            c.first = min(a.first, b.first);
            c.second = std::min(a.second, b.second);
            check("min");

            c.first = max(a.first, b.first);
            c.second = std::max(a.second, b.second);
            check("max");

            c.first = a.first % b.first;
            c.second = mod_imp(a.second, b.second);
            check("%");

            // Arithmetic with constant RHS
            if (!add_would_overflow(64, a.second, b.second)) {
                c.first = a.first + b.second;
                c.second = a.second + b.second;
                check_scalar("+");
            }

            if (!sub_would_overflow(64, a.second, b.second)) {
                c.first = a.first - b.second;
                c.second = a.second - b.second;
                check_scalar("-");
            }

            if (!mul_would_overflow(64, a.second, b.second)) {
                c.first = a.first * b.second;
                c.second = a.second * b.second;
                check_scalar("*");
            }

            c.first = a.first / b.second;
            c.second = div_imp(a.second, b.second);
            check_scalar("/");

            c.first = min(a.first, b.second);
            c.second = std::min(a.second, b.second);
            check_scalar("min");

            c.first = max(a.first, b.second);
            c.second = std::max(a.second, b.second);
            check_scalar("max");

            c.first = a.first % b.second;
            c.second = mod_imp(a.second, b.second);
            check_scalar("%");

            // Some unary operators
            c.first = -a.first;
            c.second = -a.second;
            check("unary -");

            c.first = cast(UInt(8), a.first);
            c.second = (int64_t)(uint8_t)(a.second);
            check("cast to uint8");

            c.first = cast(Int(8), a.first);
            c.second = (int64_t)(int8_t)(a.second);
            check("cast to uint8");

            // Comparison
            _halide_user_assert(!(a.first < b.first) || a.second < b.second)
                << a.first << " " << a.second << " " << b.first << " " << b.second;

            _halide_user_assert(!(a.first <= b.first) || a.second <= b.second)
                << a.first << " " << a.second << " " << b.first << " " << b.second;

            _halide_user_assert(!(a.first > b.first) || a.second > b.second)
                << a.first << " " << a.second << " " << b.first << " " << b.second;

            _halide_user_assert(!(a.first >= b.first) || a.second >= b.second)
                << a.first << " " << a.second << " " << b.first << " " << b.second;

            // Comparison against constants
            _halide_user_assert(!(a.first < b.second) || a.second < b.second)
                << a.first << " " << a.second << " " << b.second;

            _halide_user_assert(!(a.first <= b.second) || a.second <= b.second)
                << a.first << " " << a.second << " " << b.second;

            _halide_user_assert(!(a.first > b.second) || a.second > b.second)
                << a.first << " " << a.second << " " << b.second;

            _halide_user_assert(!(a.first >= b.second) || a.second >= b.second)
                << a.first << " " << a.second << " " << b.second;

            _halide_user_assert(!(a.second < b.first) || a.second < b.second)
                << a.second << " " << b.first << " " << b.second;

            _halide_user_assert(!(a.second <= b.first) || a.second <= b.second)
                << a.second << " " << b.first << " " << b.second;

            _halide_user_assert(!(a.second > b.first) || a.second > b.second)
                << a.second << " " << b.first << " " << b.second;

            _halide_user_assert(!(a.second >= b.first) || a.second >= b.second)
                << a.second << " " << b.first << " " << b.second;
        }
    }

    printf("Success!\n");
    return 0;
}
