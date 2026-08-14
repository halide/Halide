#include "Halide.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace Halide::Internal;

namespace {

int failures = 0;

void check(bool actual, bool expected, const std::string &what) {
    if (actual != expected) {
        std::cout << "FAILED: " << what << ": got " << actual << ", expected " << expected << "\n";
        failures++;
    }
}

void set_env(const char *name, const char *value) {
#ifdef _MSC_VER
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

}  // namespace

int main(int argc, char **argv) {
    // Support being re-exec'd (via run_process, below) as a trivial child
    // process that just exits with a requested code -- this lets us test
    // run_process()'s exit-code handling portably, without depending on any
    // particular external executable (e.g. /bin/true) being present, found
    // via PATH, or invocable without shell interpretation on every OS.
    // Mirrors the self-re-exec pattern used by generator_cache.cpp.
    if (argc == 3 && std::string(argv[1]) == "--exit-code") {
        return std::atoi(argv[2]);
    }

    // get_env_variable
    set_env("HALIDE_TEST_UTIL_PROCESS_VAR", "hello");
    check(get_env_variable("HALIDE_TEST_UTIL_PROCESS_VAR") == "hello", true, "get_env_variable() of a var that is set");
    check(get_env_variable("HALIDE_TEST_UTIL_PROCESS_VAR_NOT_SET") == "", true, "get_env_variable() of a var that is not set");

    // run_process
    const std::string self = std::filesystem::absolute(argv[0]).string();
    check(run_process({self, "--exit-code", "0"}) == 0, true, "run_process() of a successful child");
    check(run_process({self, "--exit-code", "7"}) == 7, true, "run_process() propagates the child's exit code");
    check(run_process({"definitely_does_not_exist_xyz_12345"}) == -1, true, "run_process() of a nonexistent executable returns -1");

    if (failures > 0) {
        std::cout << failures << " check(s) failed.\n";
        return 1;
    }

    printf("Success!\n");
    return 0;
}
