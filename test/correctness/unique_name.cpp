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

    // unique_name('$') must be treated the same as unique_name('_'), since
    // '$' is remapped to '_' before the per-prefix counter is consulted.
    // The two should therefore share a counter and never collide.
    std::string dollar_prefixed = unique_name('$');
    std::string underscore_prefixed = unique_name('_');
    if (dollar_prefixed == underscore_prefixed) {
        std::cout << "unique_name('$') collided with unique_name('_'): "
                  << '"' << dollar_prefixed << "\"\n";
        return 1;
    }

    // A string prefix that already contains a literal '$' is a many-to-one
    // input (it hashes the same as the same string with '_' substituted),
    // but must still never collide with anything else unique_name has
    // returned -- and once a numeric suffix must be appended, the literal
    // '$' is normalized away (to avoid ambiguity with the "string pattern"
    // family), so it will not reappear verbatim in later results.
    std::string has_dollar = unique_name(std::string("foo$bar"));
    std::string has_dollar_again = unique_name(std::string("foo$bar"));
    if (has_dollar == has_dollar_again) {
        std::cout << "unique_name(\"foo$bar\") produced a collision: "
                  << '"' << has_dollar << "\"\n";
        return 1;
    }

    printf("Success!\n");
    return 0;
}
