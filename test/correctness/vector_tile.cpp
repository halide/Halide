#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
Var i{"i"}, j{"j"}, io{"io"}, jo{"jo"};
}

TEST(VectorTileTest, VectorizedTile) {
    Func f;
    f(i, j) = i * j;

    f.tile({i, j}, {io, jo}, {i, j}, {8, 8}, {TailStrategy::RoundUp, TailStrategy::RoundUp});
    ASSERT_NO_THROW(f.realize({128, 128}));
}

TEST(VectorTileTest, VectorizedTileDefaultTail) {
    Func f;
    f(i, j) = i * j;

    f.tile({i, j}, {io, jo}, {i, j}, {8, 8});
    ASSERT_NO_THROW(f.realize({128, 128}));
}

TEST(VectorTileTest, StageTileDefaultTail) {
    Func f;
    f(i, j) = 0;
    f(i, j) += i * j;

    f.update(0).tile({i, j}, {io, jo}, {i, j}, {8, 8});
    ASSERT_NO_THROW(f.realize({128, 128}));
}
