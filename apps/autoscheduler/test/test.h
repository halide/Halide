#ifndef TEST_H
#define TEST_H

#include "Halide.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

#define user_assert(c) _halide_internal_assertion(c, Halide::Internal::ErrorReport::User)
#define EXPECT_EQ(expected, actual) expect_eq(__LINE__, expected, actual)

template <typename A, typename B>
void expect_eq(int line, const A& expected, const B& actual) {
    user_assert(expected == actual)
        << "Assert failed on line " << line << "."
        << "\nExpected value = " << expected
        << "\nActual value = " << actual;
}

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif // TEST_H
