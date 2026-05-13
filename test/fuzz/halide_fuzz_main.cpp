#include "halide_fuzz_main.h"
#include "fuzz_helpers.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

namespace {

template<typename T>
T initialize_rng() {
    constexpr auto kStateWords = T::state_size * sizeof(typename T::result_type) / sizeof(uint32_t);
    std::vector<uint32_t> random(kStateWords);
    std::generate(random.begin(), random.end(), std::random_device{});
    std::seed_seq seed_seq(random.begin(), random.end());
    return T{seed_seq};
}

void print_usage(const char *argv0) {
    std::cerr
        << "Usage: " << argv0 << " [options] [seed]\n"
        << "\n"
        << "Options:\n"
        << "  -runs=N             Number of fuzz iterations (default: 10000)\n"
        << "  -timeout=N          (ignored, accepted for libFuzzer compatibility)\n"
        << "  -max_total_time=N   (ignored, accepted for libFuzzer compatibility)\n"
        << "  -help               Print this help message and exit\n"
        << "\n"
        << "If a single non-option argument is given, it is used as the RNG seed.\n"
        << "Options may use '-' or '--' prefixes.\n";
}

// Strip one or two leading dashes from arg. Returns nullptr if arg doesn't
// start with '-'.
const char *strip_dashes(const char *arg) {
    if (arg[0] != '-') {
        return nullptr;
    }
    arg++;
    if (arg[0] == '-') {
        arg++;
    }
    return arg;
}

// Try to parse "key=N" where key matches `name`. On match, stores the parsed
// positive integer in *out and returns true. On parse error, prints a message
// and exits. Returns false if `body` doesn't start with `name=`.
bool parse_positive_int_flag(const char *body, const char *name, int *out) {
    size_t len = strlen(name);
    if (strncmp(body, name, len) != 0 || body[len] != '=') {
        return false;
    }
    const char *val = body + len + 1;
    int n = 0;
    std::istringstream iss(val);
    if (!(iss >> n) || !iss.eof() || n <= 0) {
        std::cerr << "Error: -" << name << " requires a strictly positive integer, got '" << val << "'\n\n";
        print_usage("fuzz_test");
        exit(1);
    }
    *out = n;
    return true;
}

}  // namespace

namespace Halide {

int fuzz_main(int argc, char **argv, FuzzFunction main_fn) {
    int runs = 10000;
    FuzzingContext::SeedType explicit_seed = 0;
    bool has_explicit_seed = false;

    // First pass: check for -help anywhere.
    for (int i = 1; i < argc; i++) {
        const char *body = strip_dashes(argv[i]);
        if (body && strcmp(body, "help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    // Second pass: parse all arguments.
    for (int i = 1; i < argc; i++) {
        if (const char *body = strip_dashes(argv[i])) {
            int dummy = 0;
            if (parse_positive_int_flag(body, "runs", &runs)) {
                continue;
            }
            if (parse_positive_int_flag(body, "timeout", &dummy)) {
                std::cerr << "Warning: -timeout is accepted but ignored.\n";
                continue;
            }
            if (parse_positive_int_flag(body, "max_total_time", &dummy)) {
                std::cerr << "Warning: -max_total_time is accepted but ignored.\n";
                continue;
            }
            std::cerr << "Error: unknown option '" << argv[i] << "'\n\n";
            print_usage(argv[0]);
            return 1;
        }

        // Positional argument: must be the only one and must be a seed.
        if (has_explicit_seed) {
            std::cerr << "Error: unexpected extra argument '" << argv[i] << "'\n\n";
            print_usage(argv[0]);
            return 1;
        }
        std::istringstream iss(argv[i]);
        if (argv[i][0] == '-' || !(iss >> explicit_seed) || !iss.eof()) {
            std::cerr << "Error: seed must be a non-negative integer, got '" << argv[i] << "'\n\n";
            print_usage(argv[0]);
            return 1;
        }
        has_explicit_seed = true;
    }

    if (has_explicit_seed) {
        // Single run with the given seed.
        std::cerr << "Seed: " << explicit_seed << "\n"
                  << std::flush;
        FuzzingContext ctx{explicit_seed};
        return main_fn(ctx);
    }

    auto seed_generator = initialize_rng<FuzzingContext::RandomEngine>();

    for (int i = 0; i < runs; i++) {
        auto seed = seed_generator();
        std::cerr << "Seed: " << seed << "\n"
                  << std::flush;
        FuzzingContext ctx{seed};
        int result = main_fn(ctx);
        if (result != 0) {
            return result;
        }
    }

    return 0;
}

}  // namespace Halide
