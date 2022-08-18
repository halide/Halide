#ifndef TEST_H
#define TEST_H

#include "Halide.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

#define user_assert(c) _halide_internal_assertion(c, Halide::Internal::ErrorReport::User)
#define EXPECT_EQ(expected, actual) expect_eq(__LINE__, expected, actual)
#define APPROX_EQ(expected, actual, epsilon) approx_eq(__LINE__, expected, actual, epsilon)
#define EXPECT(expected) expect(__LINE__, expected)

template<typename A, typename B>
void expect_eq(int line, const A &expected, const B &actual) {
    user_assert(expected == actual)
        << "Assert failed on line " << line << "."
        << "\nExpected value = " << expected
        << "\nActual value = " << actual;
}

template<typename A, typename B>
void approx_eq(int line, const A &expected, const B &actual, float epsilon) {
    user_assert(std::abs(expected - actual) < epsilon)
        << "Assert failed on line " << line << "."
        << "\nExpected value = " << expected
        << "\nActual value = " << actual;
}

template<typename A>
void expect(int line, const A &expected) {
    user_assert(expected)
        << "Assert failed on line " << line << "."
        << "\nExpected value to be true\n";
}

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // TEST_H
