#include "RunGen.h"

using namespace Halide::RunGen;
using Halide::Tools::BenchmarkConfig;

namespace {

struct RegisteredFilter {
    struct RegisteredFilter *next;
    int (*filter_argv_call)(void **);
    const struct halide_filter_metadata_t *filter_metadata;
};

RegisteredFilter *registered_filters = nullptr;

extern "C" void halide_register_argv_and_metadata(
    int (*filter_argv_call)(void **),
    const struct halide_filter_metadata_t *filter_metadata,
    const char *const *extra_key_value_pairs) {

    auto *rf = new RegisteredFilter();
    rf->next = registered_filters;
    rf->filter_argv_call = filter_argv_call;
    rf->filter_metadata = filter_metadata;
    // RunGen ignores extra_key_value_pairs
    registered_filters = rf;
}

std::string replace_all(const std::string &str,
                        const std::string &find,
                        const std::string &replace) {
    size_t pos = 0;
    std::string result = str;
    while ((pos = result.find(find, pos)) != std::string::npos) {
        result.replace(pos, find.length(), replace);
        pos += replace.length();
    }
    return result;
}

void usage(const char *argv0) {
    const std::string usage = R"USAGE(
Usage: $NAME$ argument=value [argument=value... ] [flags]

Arguments:

    Specify the Generator's input and output values by name, in any order.

    Scalar inputs are specified in the obvious syntax, e.g.

        some_int=42 some_float=3.1415

    You can also use the text `default` or `estimate` to use the default or
    estimate value of the given input, respectively. (You can join these by
    commas to give default-then-estimate or estimate-then-default behaviors.)

    Buffer inputs and outputs are specified by pathname:

        some_input_buffer=/path/to/existing/file.png
        some_output_buffer=/path/to/create/output/file.png

    We currently support JPG, PGM, PNG, PPM format. If the type or dimensions
    of the input or output file type can't support the data (e.g., your filter
    uses float32 input and output, and you load/save to PNG), we'll use the most
    robust approximation within the format and issue a warning to stdout.

    (We anticipate adding other image formats in the future, in particular,
    TIFF and TMP.)

    For inputs, there are also "pseudo-file" specifiers you can use; currently
    supported are

        zero:[NUM,NUM,...]

        This input should be an image with the given extents, and all elements
        set to zero of the appropriate type. (This is useful for benchmarking
        filters that don't have performance variances with different data.)

        constant:VALUE:[NUM,NUM,...]

        Like zero, but allows an arbitrary value of the input's type.

        identity:[NUM,NUM,...]

        This input should be an image with the given extents, where diagonal
        elements are set to one of the appropriate type, and the rest are zero.
        Diagonal elements are those whose first two coordinates are equal.

        random:SEED:[NUM,NUM,...]

        This input should be an image with the given extents, and all elements
        set to a random value of the appropriate type. The random values will
        be constructed using the mt19937_64 engine, using the given seed;
        all floating point values will be in a uniform distribution between
        0.0 and 1.0, while integral values will be uniform across the entire
        range of the type.

        (We anticipate adding other pseudo-file inputs in the future, e.g.
        various random distributions, gradients, rainbows, etc.)

        In place of [NUM,NUM,...] for boundary, you may specify 'auto'; this
        will run a bounds-query to choose a legal input size given the output
        size constraints. (In general, this is useful only when also using
        the --output_extents flag.)

        In place of [NUM,NUM,...] for boundary, you may specify 'estimate';
        this will use the estimated bounds specified in the code.

Flags:

    --help:
        print this message and exit.

    --describe:
        print names and types of all arguments to stdout and exit.

    --output_extents=[NUM,NUM,...]
        By default, we attempt to calculate a reasonable size for the output
        buffers, based on the size of the input buffers and bounds query; if we
        guess wrong, or you want to explicitly specify the desired output size,
        you can specify the extent of each dimension with this flag:

        --output_extents=[1000,100]   # 2 dimensions: w=1000 h = 100
        --output_extents=[100,200,3]  # 3 dimensions: w=100 h=200 c=3

        Note that if there are multiple outputs, all will be constrained
        to this shape.

    --verbose:
        emit extra diagnostic output.

    --quiet:
        Don't log calls to halide_print() to stdout.

    --benchmarks=all:
        Run the filter with the given arguments many times to
        produce an estimate of average execution time; this currently
        runs "samples" sets of "iterations" each, and chooses the fastest
        sample set.

    --benchmark_min_time=DURATION_SECONDS [default = 0.1]:
        Override the default minimum desired benchmarking time; ignored if
        --benchmarks is not also specified.

    --track_memory:
        Override Halide memory allocator to track high-water mark of memory
        allocation during run; note that this may slow down execution, so
        benchmarks may be inaccurate if you combine --benchmark with this.

    --default_input_buffers=VALUE:
        Specify the value for all otherwise-unspecified buffer inputs, in the
        same syntax in use above. If you omit =VALUE, "zero:auto" will be used.

    --default_input_scalars=VALUE:
        Specify the value for all otherwise-unspecified scalar inputs, in the
        same syntax in use above. If you omit =VALUE, "estimate,default"
        will be used.

    --parsable_output:
        Final output is emitted in an easy-to-parse output (one value per line),
        rather than easy-for-humans.

    --estimate_all:
        Request that all inputs and outputs are based on estimate,
        and fill buffers with random values. This is exactly equivalent to
        specifying

            --default_input_buffers=estimate_then_auto
            --default_input_scalars=estimate
            --output_extents=estimate

        and is a convenience for automated benchmarking.

    --success:
        Print "Success!" to stdout if we exit with a result code of zero.
        (This is mainly useful for use with Halide's testing infrastructure,
        which relies on this for successful tests.)

Known Issues:

    * Filters running on GPU (vs CPU) have not been tested.
    * Filters using buffer layouts other than planar (e.g. interleaved/chunky)
      may be buggy.

)USAGE";

    std::string basename = split_string(replace_all(argv0, "\\", "/"), "/").back();
    std::cout << replace_all(usage, "$NAME$", basename);
}

// Utility class for installing memory-tracking machinery into the Halide runtime
// when --track_memory is specified.
class HalideMemoryTracker {
    static HalideMemoryTracker *active;

    std::mutex tracker_mutex;

    // Total current CPU memory allocated via halide_malloc.
    // Access controlled by tracker_mutex.
    uint64_t memory_allocated{0};

    // High-water mark of CPU memory allocated since program start
    // (or last call to get_cpu_memory_highwater_reset).
    // Access controlled by tracker_mutex.
    uint64_t memory_highwater{0};

    // Map of outstanding allocation sizes.
    // Access controlled by tracker_mutex.
    std::map<void *, size_t> memory_size_map;

    void *tracker_malloc_impl(void *user_context, size_t x) {
        std::lock_guard<std::mutex> lock(tracker_mutex);

        void *ptr = halide_default_malloc(user_context, x);

        memory_allocated += x;
        if (memory_highwater < memory_allocated) {
            memory_highwater = memory_allocated;
        }
        if (memory_size_map.find(ptr) != memory_size_map.end()) {
            halide_error(user_context, "Tracking error in tracker_malloc");
        }
        memory_size_map[ptr] = x;

        return ptr;
    }

    void tracker_free_impl(void *user_context, void *ptr) {
        std::lock_guard<std::mutex> lock(tracker_mutex);
        auto it = memory_size_map.find(ptr);
        if (it == memory_size_map.end()) {
            halide_error(user_context, "Tracking error in tracker_free");
        }
        size_t x = it->second;
        memory_allocated -= x;
        memory_size_map.erase(it);
        halide_default_free(user_context, ptr);
    }

    static void *tracker_malloc(void *user_context, size_t x) {
        return active->tracker_malloc_impl(user_context, x);
    }

    static void tracker_free(void *user_context, void *ptr) {
        return active->tracker_free_impl(user_context, ptr);
    }

public:
    void install() {
        assert(!active);
        active = this;
        halide_set_custom_malloc(tracker_malloc);
        halide_set_custom_free(tracker_free);
    }

    uint64_t allocated() {
        std::lock_guard<std::mutex> lock(tracker_mutex);
        return memory_allocated;
    }

    uint64_t highwater() {
        std::lock_guard<std::mutex> lock(tracker_mutex);
        return memory_highwater;
    }

    void highwater_reset() {
        std::lock_guard<std::mutex> lock(tracker_mutex);
        memory_highwater = memory_allocated;
    }
};

/* static */ HalideMemoryTracker *HalideMemoryTracker::active{nullptr};

bool log_info = false;
bool log_warn = true;

void do_log_cout(const std::string &s) {
    std::cout << s;
}

void do_log_cerr(const std::string &s) {
    std::cerr << s;
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
    if (argc <= 1) {
        usage(argv[0]);
        return 0;
    }

    if (registered_filters == nullptr) {
        std::cerr << "No filters registered. Compile RunGenMain.cpp along with at least one 'registration' output from a generator.\n";
        return -1;
    }

    // Look for --name
    std::string filter_name;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            const char *p = argv[i] + 1;  // skip -
            if (p[0] == '-') {
                p++;  // allow -- as well, because why not
            }
            std::vector<std::string> v = split_string(p, "=");
            std::string flag_name = v[0];
            std::string flag_value = v.size() > 1 ? v[1] : "";
            // Check for the help flag early so that it takes
            // precedence over other errors that occur before full
            // argument parsing.
            if (flag_name == "help") {
                usage(argv[0]);
                return 0;
            }
            if (v.size() > 2) {
                fail() << "Invalid argument: " << argv[i];
            }
            if (flag_name != "name") {
                continue;
            }
            if (!filter_name.empty()) {
                fail() << "--name cannot be specified twice.";
            }
            filter_name = flag_value;
            if (filter_name.empty()) {
                fail() << "--name cannot be empty.";
            }
        }
    }

    auto *rf = registered_filters;
    if (filter_name.empty()) {
        // Just choose the first one.
        if (rf->next != nullptr) {
            std::ostringstream o;
            o << "Must specify --name if multiple filters are registered; registered filters are:\n";
            for (auto *rf = registered_filters; rf != nullptr; rf = rf->next) {
                o << "  " << rf->filter_metadata->name << "\n";
            }
            o << "\n";
            fail() << o.str();
        }
    } else {
        for (; rf != nullptr; rf = rf->next) {
            if (filter_name == rf->filter_metadata->name) {
                break;
            }
        }
        if (rf == nullptr) {
            std::ostringstream o;
            o << "Filter " << filter_name << " not found; registered filters are:\n";
            for (auto *rf = registered_filters; rf != nullptr; rf = rf->next) {
                o << "  " << rf->filter_metadata->name << "\n";
            }
            o << "\n";
            fail() << o.str();
        }
    }

    RunGen r(rf->filter_argv_call, rf->filter_metadata);

    std::string user_specified_output_shape;
    std::set<std::string> seen_args;
    bool benchmark = false;
    bool track_memory = false;
    bool describe = false;
    double benchmark_min_time = BenchmarkConfig().min_time;
    std::string default_input_buffers;
    std::string default_input_scalars;
    std::string benchmarks_flag_value;
    bool emit_success = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            const char *p = argv[i] + 1;  // skip -
            if (p[0] == '-') {
                p++;  // allow -- as well, because why not
            }
            std::vector<std::string> v = split_string(p, "=");
            std::string flag_name = v[0];
            std::string flag_value = v.size() > 1 ? v[1] : "";
            if (v.size() > 2) {
                fail() << "Invalid argument: " << argv[i];
            }
            if (flag_name == "name") {
                continue;
            } else if (flag_name == "verbose") {
                if (flag_value.empty()) {
                    flag_value = "true";
                }
                if (!parse_scalar(flag_value, &log_info)) {
                    fail() << "Invalid value for flag: " << flag_name;
                }
            } else if (flag_name == "quiet") {
                if (flag_value.empty()) {
                    flag_value = "true";
                }
                bool quiet;
                if (!parse_scalar(flag_value, &quiet)) {
                    fail() << "Invalid value for flag: " << flag_name;
                }
                r.set_quiet(quiet);
            } else if (flag_name == "parsable_output") {
                if (flag_value.empty()) {
                    flag_value = "true";
                }
                bool parsable_output;
                if (!parse_scalar(flag_value, &parsable_output)) {
                    fail() << "Invalid value for flag: " << flag_name;
                }
                r.set_parsable_output(parsable_output);
            } else if (flag_name == "describe") {
                if (flag_value.empty()) {
                    flag_value = "true";
                }
                if (!parse_scalar(flag_value, &describe)) {
                    fail() << "Invalid value for flag: " << flag_name;
                }
            } else if (flag_name == "track_memory") {
                if (flag_value.empty()) {
                    flag_value = "true";
                }
                if (!parse_scalar(flag_value, &track_memory)) {
                    fail() << "Invalid value for flag: " << flag_name;
                }
            } else if (flag_name == "benchmarks") {
                benchmarks_flag_value = flag_value;
                benchmark = true;
            } else if (flag_name == "benchmark_min_time") {
                if (!parse_scalar(flag_value, &benchmark_min_time)) {
                    fail() << "Invalid value for flag: " << flag_name;
                }
            } else if (flag_name == "default_input_buffers") {
                default_input_buffers = flag_value;
                if (default_input_buffers.empty()) {
                    default_input_buffers = "zero:auto";
                }
            } else if (flag_name == "default_input_scalars") {
                default_input_scalars = flag_value;
                if (default_input_scalars.empty()) {
                    default_input_scalars = "estimate,default";
                }
            } else if (flag_name == "output_extents") {
                user_specified_output_shape = flag_value;
            } else if (flag_name == "estimate_all") {
                // Equivalent to:
                // --default_input_buffers=random:0:estimate_then_auto
                // --default_input_scalars=estimate
                // --output_extents=estimate
                default_input_buffers = "random:0:estimate_then_auto";
                default_input_scalars = "estimate";
                user_specified_output_shape = "estimate";
            } else if (flag_name == "success") {
                if (flag_value.empty()) {
                    flag_value = "true";
                }
                if (!parse_scalar(flag_value, &emit_success)) {
                    fail() << "Invalid value for flag: " << flag_name;
                }
            } else {
                usage(argv[0]);
                fail() << "Unknown flag: " << flag_name;
            }
        } else {
            // Assume it's a named Input or Output for the Generator,
            // in the form name=value.
            std::vector<std::string> v = split_string(argv[i], "=");
            if (v.size() != 2 || v[0].empty() || v[1].empty()) {
                fail() << "Invalid argument: " << argv[i];
            }
            r.parse_one(v[0], v[1], &seen_args);
        }
    }

    if (describe) {
        r.describe();
        return 0;
    }

    // It's OK to omit output arguments when we are benchmarking or tracking memory.
    bool ok_to_omit_outputs = (benchmark || track_memory);

    if (benchmark && track_memory) {
        warn() << "Using --track_memory with --benchmarks will produce inaccurate benchmark results.";
    }

    // Check to be sure that all required arguments are specified.
    r.validate(seen_args, default_input_buffers, default_input_scalars, ok_to_omit_outputs);

    // Parse all the input arguments, loading images as necessary.
    // (Don't handle outputs yet.)
    r.load_inputs(user_specified_output_shape);

    // Run a bounds query: we need to figure out how to allocate the output buffers,
    // and the input buffers might need reshaping to satisfy constraints (e.g. a chunky/interleaved layout).
    std::vector<Shape> constrained_shapes = r.run_bounds_query();

    r.adapt_input_buffers(constrained_shapes);
    r.allocate_output_buffers(constrained_shapes);

    // If we're tracking memory, install the memory tracker *after* doing a bounds query.
    HalideMemoryTracker tracker;
    if (track_memory) {
        tracker.install();
    }

    // This is a single-purpose binary to benchmark this filter, so we
    // shouldn't be eagerly returning device memory.
    if (auto result = halide_reuse_device_allocations(nullptr, true); result != halide_error_code_success) {
        std::cerr << "halide_reuse_device_allocations() returned an error: " << (int)result << "\n";
    }

    if (benchmark) {
        if (benchmarks_flag_value.empty()) {
            benchmarks_flag_value = "all";
        }
        if (benchmarks_flag_value != "all") {
            fail() << "The only valid value for --benchmarks is 'all'";
        }
        r.run_for_benchmark(benchmark_min_time);
    } else {
        r.run_for_output();
    }

    if (track_memory) {
        // Ensure that we copy any GPU-output buffers back to host before
        // we report on memory usage.
        if (auto result = r.copy_outputs_to_host(); result != halide_error_code_success) {
            std::cerr << "Warning: copy_outputs_to_host() returned error " << (int)result << "\n";
        }
        std::cout << "Maximum Halide memory: " << tracker.highwater()
                  << " bytes for output of " << r.megapixels_out() << " mpix.\n";
    }

    // Save the output(s), if necessary.
    r.save_outputs();

    if (emit_success) {
        std::cout << "Success!\n";
    }

    return 0;
}
