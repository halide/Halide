// Exercises Internal::run_process()'s stdout/stderr redirection overload.
// Each check re-execs this test binary with --child so that the process
// being redirected is a real child process, not just an in-process write.

#include "Halide.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace Halide;

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

constexpr int kChildExitCode = 3;
const std::string kStdoutMarker = "hello from stdout\n";
const std::string kStderrMarker = "hello from stderr\n";

// Child mode: write known markers to stdout and stderr, then exit with a
// known nonzero code. stdout is flushed before writing to stderr (which is
// unbuffered by default) so that when both streams share a single file, the
// resulting order is deterministic.
int run_child() {
    std::fputs(kStdoutMarker.c_str(), stdout);
    std::fflush(stdout);
    std::fputs(kStderrMarker.c_str(), stderr);
    return kChildExitCode;
}

std::string slurp(const std::string &path) {
    std::vector<char> data = Internal::read_entire_file(path);
    return {data.begin(), data.end()};
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--child") {
        return run_child();
    }

    const std::string self = fs::absolute(argv[0]).string();

    // Separate files for stdout and stderr: each should contain exactly its
    // own marker, and the exit code should be preserved.
    {
        Internal::TemporaryFile out("run_process_out", ".txt");
        Internal::TemporaryFile err("run_process_err", ".txt");

        int rc = Internal::run_process({self, "--child"}, out.pathname(), err.pathname());
        check(rc == kChildExitCode);
        check(slurp(out.pathname()) == kStdoutMarker);
        check(slurp(err.pathname()) == kStderrMarker);
    }

    // Same file for both streams: the two markers should be concatenated in
    // write order, not corrupted or partially overwritten (which is what
    // would happen if the same path were opened twice independently instead
    // of sharing one fd).
    {
        Internal::TemporaryFile combined("run_process_combined", ".txt");

        int rc = Internal::run_process({self, "--child"}, combined.pathname(), combined.pathname());
        check(rc == kChildExitCode);
        check(slurp(combined.pathname()) == kStdoutMarker + kStderrMarker);
    }

    // Empty paths (including the plain 1-arg overload, which forwards to
    // this one) must still work exactly as before: no redirection, just the
    // exit code.
    {
        int rc = Internal::run_process({self, "--child"}, "", "");
        check(rc == kChildExitCode);

        rc = Internal::run_process({self, "--child"});
        check(rc == kChildExitCode);
    }

    std::cout << "Success!\n";
    return 0;
}
