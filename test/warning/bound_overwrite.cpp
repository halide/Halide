// Calling bound_extent() twice for the same Var with different values
// silently replaced the first call's constraint before Func::bound()
// started merging same-Var Bound entries. Now that a second call updates
// the existing Bound in place instead of appending an independent one, a
// user_warning flags the overwrite, since it's usually a scheduling mistake
// rather than something intentional.
#include "Halide.h"
#include "halide_test_dirs.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x");
    f(x) = x;

    f.bound_extent(x, 3);
    f.bound_extent(x, 4);

    return 0;
}
