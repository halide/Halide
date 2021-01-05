#include "Halide.h"
#include <map>
#include <random>
#include <stdio.h>
#include <string>
#include <time.h>

// Test operators in IROperator.h by passing in fuzzed values.

namespace {

using std::map;
using std::string;
using namespace Halide;
using namespace Halide::Internal;

// TODO: Should we using 64 bit types?
Type fuzz_types[] = {UInt(1), UInt(8), UInt(16), UInt(32), Int(8), Int(16), Int(32), Float(32), Float(64)};
const int fuzz_type_count = sizeof(fuzz_types) / sizeof(fuzz_types[0]);
// Used for substitution (casts do eager folding).
const std::string var_name = "a";

// Use std::mt19937 instead of rand() to ensure consistent behavior on all systems.
std::mt19937 rng(0);

// Part of random_leaf in fuzz_simplify.cpp
Expr random_value(Type t) {
    if (t.is_int() && t.bits() == 32) {
        // For Int(32), we don't care about correctness during
        // overflow, so just use numbers that are unlikely to
        // overflow.
        return cast(t, (int)(rng() % 256 - 128));
    } else {
        return cast(t, (int)(rng() - RAND_MAX / 2));
    }
}


// Taken from fuzz_simplify.cpp.
// TODO: make a header for fuzzing?
Type random_type(int width = 1) {
    Type t = fuzz_types[rng() % fuzz_type_count];

    if (width > 1) {
        t = t.with_lanes(width);
    }
    return t;
}

// This is similar to fuzz_simplify.cpp's Expr generator
// but it's only for casts.
// t is the type of the innermost Expr.
Expr random_cast(Type t, int depth = 0) {
    if (depth > 0) {
        Type rt = random_type();
        Expr casted = random_cast(t, depth - 1);
        return cast(rt, casted);
    } else {
        return Expr(Variable::make(t, var_name));
    }
}

bool test_signed_operators(Expr test, Type t, int samples) {
    map<string, Expr> vars;
    for (int i = 0; i < samples; i++) {
        // Random value to substitute for the innermost.
        Expr value = random_value(t);
        vars[var_name] = value;
        Expr expr = substitute(vars, test);
        Expr simpl = simplify(expr);

        if (is_positive_const(expr) != is_positive_const(simpl)) {
            std::cerr << "Signs (+) don't match for: " << expr << " and " << simpl << "\n";
            std::cerr << is_positive_const(expr) << " != " << is_positive_const(simpl) << "\n";
            std::cerr << "Original: " << test << "\n";
            std::cerr << var_name << " = " << value << "\n";
            return false;
        } else if (is_negative_const(expr) != is_negative_const(simpl)) {
            std::cerr << "Signs (-) don't match for: " << expr << " and " << simpl << "\n";
            std::cerr << is_negative_const(expr) << " != " << is_negative_const(simpl) << "\n";
            std::cerr << "Original: " << test << "\n";
            std::cerr << var_name << " = " << value << "\n";
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    // Number of random expressions to test.
    const int count = 10000;
    // Maximum depth of cast chain.
    const int max_depth = 5;
    // Number of samples to test the cast chain for.
    const int samples = 5;

    // We want different fuzz tests every time, to increase coverage.
    // We also report the seed to enable reproducing failures.
    int fuzz_seed = argc > 1 ? atoi(argv[1]) : time(nullptr);
    rng.seed(fuzz_seed);
    std::cout << "IROperator fuzz test seed: " << fuzz_seed << "\n";

    for (int n = 0; n < count; n++) {
        int depth = rng() % max_depth;
        // Choose the type for the innermost Expr.
        Type rt = random_type();
        // Generate random cast chain.
        Expr test = random_cast(rt, depth);
        if (!test_signed_operators(test, rt, samples)) {
            return -1;
        }
    }

    std::cout << "Success!\n";
    return 0;
}