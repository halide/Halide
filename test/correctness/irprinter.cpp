#include "Halide.h"
#include <gtest/gtest.h>

#include <sstream>
#include <string_view>

using namespace Halide;

TEST(IRPrinterTest, EmptyRDom) {
    std::ostringstream os;
    os << RDom();
    EXPECT_EQ(os.str(), "RDom()");
}
