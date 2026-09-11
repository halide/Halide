// Func::bound()/bound_extent()/align_bounds()/align_extent() each append a
// constraint to a Var's Bound. Multiple calls for the same Var must merge
// into a single Bound record, with a result that doesn't depend on which
// order the calls were made in -- BoundsInference (and every other consumer
// of FuncSchedule::bounds()) only ever expects to see one Bound per Var.
#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

const Bound &the_bound(const Func &f, const std::string &var_name) {
    const std::map<std::string, Bound> &bounds = f.function().schedule().bounds();
    auto it = bounds.find(var_name);
    if (it == bounds.end()) {
        printf("Expected exactly one Bound for \"%s\", found 0\n", var_name.c_str());
        exit(1);
    }
    return it->second;
}

void expect_extent_bound(const Func &f, const std::string &var_name, int extent) {
    const Bound &b = the_bound(f, var_name);
    if (!b.extent.defined() || !is_const(simplify(b.extent), extent)) {
        printf("Expected extent bound %d on \"%s\", got %s\n",
               extent, var_name.c_str(), b.extent.defined() ? "a different value" : "undefined");
        exit(1);
    }
}

void expect_alignment(const Func &f, const std::string &var_name, int modulus, int remainder) {
    const Bound &b = the_bound(f, var_name);
    if (!b.modulus.defined() || !is_const(simplify(b.modulus), modulus)) {
        printf("Expected alignment modulus %d on \"%s\", got %s\n",
               modulus, var_name.c_str(), b.modulus.defined() ? "a different value" : "undefined");
        exit(1);
    }
    if (!b.remainder.defined() || !is_const(simplify(b.remainder), remainder)) {
        printf("Expected alignment remainder %d on \"%s\", got %s\n",
               remainder, var_name.c_str(), b.remainder.defined() ? "a different value" : "undefined");
        exit(1);
    }
}

}  // namespace

int main(int argc, char **argv) {
    Var x("x"), y("y");

    // bound_extent() then align_bounds(): must merge into one Bound with
    // both the extent and the alignment set.
    {
        Func f("f");
        f(x) = x;
        f.bound_extent(x, 3);
        f.align_bounds(x, 3, 1);
        expect_extent_bound(f, "x", 3);
        expect_alignment(f, "x", 3, 1);
    }

    // The same two calls, in the opposite order: same merged result.
    {
        Func f("f");
        f(x) = x;
        f.align_bounds(x, 3, 1);
        f.bound_extent(x, 3);
        expect_extent_bound(f, "x", 3);
        expect_alignment(f, "x", 3, 1);
    }

    // bound()/align_bounds() interleaved across two Vars: each Var still
    // ends up with exactly one merged Bound, unaffected by the other Var's
    // calls in between.
    {
        Func f("f");
        f(x, y) = x + y;
        f.align_bounds(x, 4, 2);
        f.bound(y, 0, 5);
        f.bound_extent(x, 4);
        f.align_bounds(y, 5, 0);
        expect_extent_bound(f, "x", 4);
        expect_alignment(f, "x", 4, 2);
        expect_alignment(f, "y", 5, 0);
        const Bound &by = the_bound(f, "y");
        if (!by.min.defined() || !is_const(simplify(by.min), 0)) {
            printf("Expected min bound 0 on \"y\"\n");
            return 1;
        }
        if (!by.extent.defined() || !is_const(simplify(by.extent), 5)) {
            printf("Expected extent bound 5 on \"y\"\n");
            return 1;
        }
    }

    // align_extent() only sets modulus, so it updates just that field on an
    // existing Bound and leaves a remainder set by a prior align_bounds()
    // alone -- merging never clobbers a field a call didn't itself set.
    {
        Func f("f");
        f(x) = x;
        f.align_bounds(x, 3, 1);
        f.align_extent(x, 4);
        const Bound &b = the_bound(f, "x");
        if (!b.modulus.defined() || !is_const(simplify(b.modulus), 4)) {
            printf("Expected alignment modulus 4 on \"x\" after align_extent(), got %s\n",
                   b.modulus.defined() ? "a different value" : "undefined");
            return 1;
        }
        if (!b.remainder.defined() || !is_const(simplify(b.remainder), 1)) {
            printf("Expected align_extent() to leave the remainder set by a prior align_bounds() alone\n");
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
