// Exercises HL_DEBUG_CODEGEN_LOG_FILE: debug() output can be redirected to a
// file (appending, never truncating) instead of stderr, and /dev/stdout,
// /dev/stderr are recognized explicitly so they work as values even on
// platforms with no /dev filesystem to fall back on (e.g. Windows).
//
// The env var is read once per process (cached in a function-local static,
// like HL_DEBUG_CODEGEN itself), so each scenario below runs in a freshly
// spawned child process (by re-exec'ing this test with --child).

#include "Halide.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>

using namespace Halide;
using namespace Halide::Internal;

namespace {

namespace fs = std::filesystem;

// Return 1 from main() on failure; the test harness treats that as a failure.
// (assert() is compiled out in release builds, so we can't rely on it.)
#define check(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::cerr << "FAILED: " #cond " (line " << __LINE__ << ")\n"; \
            return 1;                                                     \
        }                                                                 \
    } while (0)

std::string read_all(const fs::path &p) {
    std::ifstream f(p);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void set_env(const char *name, const std::string &val) {
#ifdef _WIN32
    _putenv_s(name, val.c_str());
#else
    setenv(name, val.c_str(), /*overwrite*/ 1);
#endif
}

void unset_env(const char *name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

}  // namespace

int main(int argc, char **argv) {
    // HL_DEBUG_CODEGEN is read once per process (cached in a function-local
    // static) on first use. Clear any ambient value from the invoking shell
    // before it's read anywhere below (parent or re-exec'd --child alike):
    // an inherited tag-only rule would otherwise silently suppress the
    // untagged debug(0) call in --child mode.
    unset_env("HL_DEBUG_CODEGEN");

    // Child mode: emit one line of debug(0) output, honoring
    // HL_DEBUG_CODEGEN_LOG_FILE as inherited from the parent's environment.
    if (argc == 2 && std::string(argv[1]) == "--child") {
        debug(0) << "marker\n";
        return 0;
    }

    // HL_DEBUG_CODEGEN_LOG_FILE is also read once per process, but unlike
    // HL_DEBUG_CODEGEN above, --child is meant to inherit whatever value the
    // parent (here) sets for each scenario below, so it's only cleared in
    // the parent, before the first scenario overrides it.
    unset_env("HL_DEBUG_CODEGEN_LOG_FILE");

    const std::string self = fs::absolute(argv[0]).string();
    const fs::path tmp =
        fs::temp_directory_path() / ("hldbglog_" + std::to_string(std::random_device{}()));
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    const fs::path log = tmp / "log.txt";
    const fs::path child_out = tmp / "child_stdout.txt";
    const fs::path child_err = tmp / "child_stderr.txt";

    // Redirecting to a file: the marker lands in the file, not on stderr.
    set_env("HL_DEBUG_CODEGEN_LOG_FILE", log.string());
    check(Internal::run_process({self, "--child"}, "", child_err.string()) == 0);
    check(read_all(log) == "marker\n");
    check(read_all(child_err).empty());

    // A second run appends rather than truncating.
    check(Internal::run_process({self, "--child"}, "", child_err.string()) == 0);
    check(read_all(log) == "marker\nmarker\n");

    // /dev/stdout and /dev/stderr are recognized explicitly, rather than
    // treated as ordinary paths to open.
    set_env("HL_DEBUG_CODEGEN_LOG_FILE", "/dev/stdout");
    check(Internal::run_process({self, "--child"}, child_out.string(), "") == 0);
    check(read_all(child_out) == "marker\n");

    set_env("HL_DEBUG_CODEGEN_LOG_FILE", "/dev/stderr");
    check(Internal::run_process({self, "--child"}, "", child_err.string()) == 0);
    check(read_all(child_err) == "marker\n");

    fs::remove_all(tmp);
    std::cout << "Success!\n";
    return 0;
}
