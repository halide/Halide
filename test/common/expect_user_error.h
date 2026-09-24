#ifndef EXPECT_USER_ERROR_H
#define EXPECT_USER_ERROR_H

// Support for tests that check that a bad schedule is rejected, and rejected
// for the stated reason rather than by crashing or by an internal assert.

#include "Halide.h"

#include <stdio.h>
#include <string>

/** Run `body` and check it produces a Halide error (compile-time or
 * run-time). If `substring` is given, the message has to mention it, which is
 * how a test says it wants the error for the thing it broke and not some
 * other error. Returns whether it did, and prints why not if it didn't. */
template<typename F>
bool expect_user_error(const char *name, const char *substring, F body) {
    try {
        body();
    } catch (const Halide::Error &e) {
        std::string msg = e.what();
        if (substring && msg.find(substring) == std::string::npos) {
            printf("[%s] FAIL: error did not mention \"%s\":\n%s\n",
                   name, substring, msg.c_str());
            return false;
        }
        printf("[%s] OK: %s\n", name, msg.c_str());
        return true;
    } catch (...) {
        printf("[%s] FAIL: expected a Halide::Error but got a different exception\n", name);
        return false;
    }
    printf("[%s] FAIL: expected a user error but none was raised\n", name);
    return false;
}

/** Run `body` and check it produces a Halide user error, whatever it says. */
template<typename F>
bool expect_user_error(const char *name, F body) {
    return expect_user_error(name, nullptr, body);
}

/** Convenience wrapper for the common shape of a `main()` in a test that
 * expects a single Halide error: skips (returning 0) if Halide was built
 * without exceptions, otherwise runs `body`, checks that it throws a
 * Halide::Error mentioning `substring`, and prints "Success!" and returns 0
 * on success, or returns 1 with a diagnostic on failure. Intended to be used
 * directly as `return error_test(...);` from `main()`. */
template<typename F>
int error_test(const char *name, const char *substring, F body) {
#ifndef HALIDE_WITH_EXCEPTIONS
    printf("[SKIP] %s requires exceptions\n", name);
    return 0;
#else
    if (!expect_user_error(name, substring, body)) {
        return 1;
    }
    printf("Success!\n");
    return 0;
#endif
}

#endif
