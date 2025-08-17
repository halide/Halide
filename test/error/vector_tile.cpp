#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestVectorTile() {
    // Test reporting of mismatched sizes error in vector-of-strategies variant
    Var i, j;

    Func f;
    f(i, j) = i * j;

    Var io, jo;
    // Should result in an error
    // Bad because the vector lengths don't match
    f.tile({i, j}, {io, jo}, {i, j}, {8, 8}, {TailStrategy::RoundUp, TailStrategy::RoundUp, TailStrategy::RoundUp});
}
}  // namespace

TEST(ErrorTests, VectorTile) {
    EXPECT_COMPILE_ERROR(TestVectorTile, MatchesPattern(R"(Vectors passed to Stage::tile must all be the same length\.)"));
}
