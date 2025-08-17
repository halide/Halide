#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestAtomicsSelfReference() {
    Func f;
    Var x;
    RDom r(0, 100);

    f(x) = 0;
    f(clamp(f(r), 0, 100)) = f(r) + 1;

    f.compute_root();
    f.update()
        .atomic(true /* override_associativity_test */)
        .parallel(r);

    // f references itself on the index, making the atomic illegal.
    Realization out = f.realize({100});
}
}  // namespace

TEST(ErrorTests, AtomicsSelfReference) {
    EXPECT_COMPILE_ERROR(TestAtomicsSelfReference, MatchesPattern(R"(Can't use atomic\(\) on an update where the index written to depends on the current value of the Func)"));
}
