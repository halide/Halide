#include <iostream>

#include "HalideRuntime.h"
#include "RunGen.h"

#include "example.h"

using namespace Halide::RunGen;
using namespace Halide::Runtime;

void check(bool b, const char *msg = "Failure!") {
    if (!b) {
        std::cerr << msg << "\n";
        exit(1);
    }
}

extern "C" void halide_register_argv_and_metadata(
    int (*filter_argv_call)(void **),
    const struct halide_filter_metadata_t *filter_metadata,
    const char *const *extra_key_value_pairs) {

    check(filter_argv_call == example_argv);
    check(filter_metadata == example_metadata());
    check(extra_key_value_pairs == nullptr);
}

namespace {

std::ostream *capture_cout = nullptr;
std::ostream *capture_cerr = nullptr;

bool log_info = false;
bool log_warn = true;

void do_log_cout(const std::string &s) {
    *capture_cout << s;
}

void do_log_cerr(const std::string &s) {
    *capture_cerr << s;
}

void do_log_info(const std::string &s) {
    if (log_info) {
        do_log_cerr(s);
    }
}

void do_log_warn(const std::string &s) {
    if (log_warn) {
        do_log_cerr("Warning: " + s);
    }
}

void do_log_fail(const std::string &s) {
    do_log_cerr(s);
    abort();
}

}  // namespace

namespace Halide {
namespace RunGen {

Logger log() {
    return {do_log_cout, do_log_info, do_log_warn, do_log_fail};
}

}  // namespace RunGen
}  // namespace Halide

int main(int argc, char **argv) {
    RunGen r(example_argv, example_metadata());

    check(r.get_halide_argv_call() == example_argv);
    check(r.get_halide_metadata() == example_metadata());

    {
        std::ostringstream out, err;
        capture_cout = &out;
        capture_cerr = &err;

        r.describe();

        check(err.str() == "");

        const char *expected_out = R"DESC(Filter name: "example"
  Input "runtime_factor" is of type float32
  Output "output" is of type Buffer<int32> with 3 dimensions
)DESC";
        check(out.str() == expected_out);
    }

    // TODO: add more here; all this does is verify that we can instantiate correctly
    // and that 'describe' parses the metadata as expected.

    std::cout << "Success!\n";
    return 0;
}
