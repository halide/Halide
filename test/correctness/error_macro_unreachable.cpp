#include <Halide.h>

#include <cstdio>
#include <string>

enum class Example {
    A,
    B,
    C
};

// If the error macros are implemented correctly, it should be possible
// to determine that example_to_string returns a value in all non-error
// cases.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic error "-Wreturn-type"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wreturn-type"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(error : 4715)
#endif

std::string example_to_string1(const Example e) {
    switch (e) {
    case Example::A:
        return "A";
    case Example::B:
        return "B";
        // Oops, missing Example::C.
    default:
        break;
    }
    internal_error << "Unreachable\n";
}

std::string example_to_string2(const Example e) {
    switch (e) {
    case Example::A:
        return "A";
    case Example::B:
        return "B";
        // Oops, missing Example::C.
    default:
        break;
    }
    internal_assert(false) << "Unreachable\n";
}

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

int main() {
    printf("Success!\n");
    return 0;
}
