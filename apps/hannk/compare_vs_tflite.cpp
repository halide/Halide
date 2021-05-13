#include <cstring>
#include <functional>
#include <unistd.h>
#include <vector>

#include "util/model_runner.h"

namespace {

using FlagFn = std::function<int(const std::string &value)>;
using FlagFnMap = std::map<std::string, FlagFn>;

int process_args(int argc, char **argv, const FlagFnMap &m, FlagFn nonflags) {
    for (int i = 1; i < argc; i++) {
        std::string flag = argv[i];
        if (flag[0] != '-') {
            nonflags(flag);
            continue;
        }
        flag = flag.substr(1);
        if (flag[0] == '-') {
            flag = flag.substr(1);
        }

        std::string value;
        auto eq = flag.find('=');
        if (eq != std::string::npos) {
            value = flag.substr(eq + 1);
            flag = flag.substr(0, eq);
        } else if (i + 1 < argc) {
            value = argv[++i];
        } else {
            std::cerr << "Missing value for flag '" << flag << "'\n";
            return -1;
        }
        auto it = m.find(flag);
        if (it == m.end()) {
            std::cerr << "Unknown flag '" << flag << "'\n";
            return -1;
        }
        int r = it->second(value);
        if (r != 0) {
            return r;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    using namespace hannk;

    int seed = time(nullptr);

    ModelRunner runner;
    std::vector<std::string> files;

    // Default the exernal delegate to disabled, since it may
    // need extra setup to work (eg LD_LIBRARY_PATH or --external_delegate_path)
    runner.do_run[ModelRunner::kExternalDelegate] = false;

    const FlagFn nonflags = [&files](const std::string &value) {
        // Assume it's a file.
        files.push_back(value);
        return 0;
    };

    const FlagFnMap m = {
        {"benchmark", [&runner](const std::string &value) {
             runner.do_benchmark = std::stoi(value) != 0;
             return 0;
         }},
        {"compare", [&runner](const std::string &value) {
             runner.do_compare_results = std::stoi(value) != 0;
             return 0;
         }},
        {"enable", [&runner](const std::string &value) {
             for (int i = 0; i < ModelRunner::kNumRuns; i++) {
                 runner.do_run[i] = false;
             }
             for (char c : value) {
                 switch (c) {
                 case 't':
                     runner.do_run[ModelRunner::kTfLite] = true;
                     break;
                 case 'h':
                     runner.do_run[ModelRunner::kHannk] = true;
                     break;
                 case 'x':
                     runner.do_run[ModelRunner::kExternalDelegate] = true;
                     break;
                 case 'i':
                     runner.do_run[ModelRunner::kInternalDelegate] = true;
                     break;
                 default:
                     std::cerr << "Unknown option to --enable: " << c << "\n";
                     return -1;
                 }
             }
             return 0;
         }},
        {"external_delegate_path", [&runner](const std::string &value) {
             runner.external_delegate_path = value;
             return 0;
         }},
        {"seed", [&seed](const std::string &value) {
             seed = std::stoi(value);
             return 0;
         }},
        {"threads", [&runner](const std::string &value) {
             runner.threads = std::stoi(value);
             return 0;
         }},
        {"tolerance", [&runner](const std::string &value) {
             runner.tolerance = std::stof(value);
             return 0;
         }},
        {"verbose", [&runner](const std::string &value) {
             runner.verbosity = std::stoi(value);
             return 0;
         }},
    };

    int r = process_args(argc, argv, m, nonflags);
    if (r != 0) {
        return r;
    }

    if (runner.threads <= 0) {
#ifdef _WIN32
        char *num_cores = getenv("NUMBER_OF_PROCESSORS");
        runner.threads = num_cores ? atoi(num_cores) : 8;
#else
        runner.threads = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    }

    runner.set_seed(seed);
    runner.status();

    for (auto f : files) {
        runner.run(f);
        halide_profiler_report(nullptr);
        halide_profiler_reset();
        std::cout << "\n";
    }

    std::cout << "Done!\n";
    return 0;
}
