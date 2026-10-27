#include "Halide.h"

#include <cstdio>

using namespace Halide::Internal;

int main(int argc, char **argv) {
    // unique_name() must never return a string it has already returned
    std::string name = unique_name("foo");    // "foo"
    std::string first = unique_name(name);    // "foo$1"
    std::string second = unique_name(first);  // "foo_1$0"

    if (name == first || first == second || name == second) {
        std::cout << "unique_name() produced a collision: "
                  << '"' << name << "\", "
                  << '"' << first << "\", "
                  << '"' << second << "\"\n";
        return 1;
    }

    printf("Success!\n");
    return 0;
}
