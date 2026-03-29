#include "Halide.h"
#include <iostream>
#include <sstream>
#include <string_view>

using namespace Halide;

void assert_strings_equal(const std::string_view expected, const std::string_view actual) {
    if (expected != actual) {
        std::cerr << "Expected: " << expected << ", actual: " << actual << "\n";
        std::exit(1);
    }
}

void test_empty_rdom() {
    std::ostringstream os;
    os << RDom();
    assert_strings_equal("RDom()", os.str());
}

int main(int argc, char **argv) {
    test_empty_rdom();

    std::cout << "Success!\n";
    return 0;
}
