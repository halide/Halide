#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

#include "Deserialization.h"
#include "Func.h"
#include "Halidoscope.h"
#include "JITModule.h"
#include "Pipeline.h"
#include "Serialization.h"
#include "Util.h"

#include "../tools/halide_trace_compression.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Halide {

using namespace Internal;

namespace {

// Compresses a stream of trace packets into a file.
class TraceFileWriter {
    std::ofstream out;
    Tools::TraceCompressor compressor;
    std::vector<uint8_t> compressed;
    int64_t limit_bytes;

    bool emit() {
        out.write((const char *)compressed.data(), compressed.size());
        limit_bytes -= compressed.size();
        compressed.clear();
        return limit_bytes >= 0;
    }

public:
    explicit TraceFileWriter(const std::string &path, int64_t limit_bytes)
        : out(path, std::ios::binary), limit_bytes(limit_bytes) {
        user_assert(out.good()) << "halidoscope: unable to open " << path << " for writing\n";
    }

    bool write(const void *data, size_t size) {
        if (limit_bytes < 0) {
            return false;
        }
        bool ok = compressor.write(data, size, compressed);
        internal_assert(ok) << "halidoscope: invalid trace packet\n";
        return emit();
    }

    void finish() {
        if (limit_bytes >= 0) {
            bool ok = compressor.finish(compressed);
            internal_assert(ok) << "halidoscope: truncated trace packet\n";
            emit();
        }
        out.close();
    }
};

int halidoscope_trace_fail(JITUserContext *, const halide_trace_event_t *) {
    return halide_error_code_trace_failed;
}

// Errors reported by the pipeline during the trace run, held until the trace
// has been cleaned up.
std::string halidoscope_trace_errors;
std::mutex halidoscope_trace_errors_mutex;

void halidoscope_record_error(JITUserContext *, const char *msg) {
    std::scoped_lock lock(halidoscope_trace_errors_mutex);
    halidoscope_trace_errors += msg;
}

// Compresses everything written to the returned file descriptor into path on
// a background thread, until the descriptor is closed.
class TracePipeWriter {
    int read_fd = -1, write_fd = -1;
    TraceFileWriter writer;
    std::thread thread;

public:
    explicit TracePipeWriter(const std::string &path, int64_t limit_bytes, JITUserContext *user_context)
        : writer(path, limit_bytes) {
        int fds[2];
#ifdef _WIN32
        int rc = _pipe(fds, 1 << 20, _O_BINARY);
#else
        int rc = pipe(fds);
#endif
        user_assert(rc == 0) << "halidoscope: unable to create a pipe for the trace";
        read_fd = fds[0];
        write_fd = fds[1];
        thread = std::thread([this, user_context]() {
            std::vector<char> buf(1 << 20);
            while (true) {
#ifdef _WIN32
                int n = _read(read_fd, buf.data(), (unsigned)buf.size());
#else
                ssize_t n = read(read_fd, buf.data(), buf.size());
#endif
                if (n <= 0) {
                    break;
                }
                if (!writer.write(buf.data(), n)) {
                    // Make the next trace call fail the pipeline, but keep
                    // draining the pipe so trace calls in flight don't block.
                    user_context->handlers.custom_trace = halidoscope_trace_fail;
                }
            }
        });
    }

    TracePipeWriter(const TracePipeWriter &) = delete;
    TracePipeWriter &operator=(const TracePipeWriter &) = delete;

    int fd() const {
        return write_fd;
    }

    ~TracePipeWriter() {
        JITSharedRuntime::set_trace_file(-1);
#ifdef _WIN32
        _close(write_fd);
#else
        close(write_fd);
#endif
        thread.join();
#ifdef _WIN32
        _close(read_fd);
#else
        close(read_fd);
#endif
        writer.finish();
    }
};

}  // namespace

namespace Internal {

void pipeline_halidoscope(const Pipeline &pipeline,
                          JITUserContext *context,
                          const HalidoscopePrepareFn &prepare_trace,
                          const HalidoscopePrepareFn &prepare_profile,
                          const HalidoscopeOptions &options,
                          const Target &target_arg) {
    user_assert(pipeline.defined()) << "Pipeline is undefined\n";

    JITUserContext local_context;
    if (!context) {
        context = &local_context;
    }

    std::string halidoscope_path = options.path.value_or("halidoscope");
    const std::optional<int> &profile_runs = options.profile_runs;

    user_assert(profile_runs.value_or(0) >= 0)
        << "halidoscope: HalidoscopeOptions::profile_runs must be a non-negative integer, got "
        << *profile_runs << ".\n";

    const bool profiling = profile_runs.value_or(1) > 0;

    // Fail fast on a missing explicit path. A bare name is looked up on
    // $PATH, so it can only be checked by trying to launch it.
    if (halidoscope_path.find('/') != std::string::npos) {
        user_assert(file_exists(halidoscope_path))
            << "halidoscope: no file found at HalidoscopeOptions::path='"
            << halidoscope_path << "'.\n";
    }

    // A Target with unknowns is replaced wholesale by the JIT target from the
    // environment, which would drop the features added below.
    Target base_target = target_arg.has_unknowns() ? get_jit_target_from_environment() : target_arg;

    std::map<std::string, Parameter> external_params;
    std::vector<uint8_t> data;
    serialize_pipeline(pipeline, data, external_params);

    std::string dir = options.output_dir ? *options.output_dir : dir_make_temp();
    std::string trace_path = dir + "/trace.hltrace";
    std::string profile_path = dir + "/profile.json";
    std::string stmt_path = dir + "/conceptual_stmt.html";

    struct ScopedCleanup {
        bool armed;
        std::string trace_path, profile_path, stmt_path, dir;
        ScopedCleanup(bool armed, const std::string &tp, const std::string &pp, const std::string &sp, const std::string &d)
            : armed(armed), trace_path(tp), profile_path(pp), stmt_path(sp), dir(d) {
        }
        void run() {
            if (armed) {
                ensure_no_file_exists(trace_path);
                ensure_no_file_exists(profile_path);
                ensure_no_file_exists(stmt_path);
                dir_rmdir(dir);
                armed = false;
            }
        }
        ~ScopedCleanup() {
            run();
        }
    } scoped_cleanup(!options.output_dir, trace_path, profile_path, stmt_path, dir);

    {
        Pipeline p = deserialize_pipeline(data, external_params);
        p.compile_to_conceptual_stmt(stmt_path, p.infer_arguments(), StmtOutputFormat::HTML, base_target);
    }

    // --- Trace run: every Func's loads/stores/realizations, dumped to a
    // compressed binary trace file. ---
    {
        Pipeline traced = deserialize_pipeline(data, external_params);
        traced.trace_pipeline();
        Target trace_target = base_target
                                  .with_feature(Target::TraceLoads)
                                  .with_feature(Target::TraceStores)
                                  .with_feature(Target::TraceRealizations)
                                  .with_feature(Target::TraceBoundsRequired);

        // Route pipeline errors to halidoscope_record_error, unless the
        // caller has their own error handler, so that realize() returns and
        // the trace can be cleaned up before reporting them.
        struct RestoreHandlers {
            JITUserContext *context;
            JITHandlers handlers;
            ~RestoreHandlers() {
                context->handlers = handlers;
            }
        } restore_handlers{context, context->handlers};
        if (!context->handlers.custom_error ||
            context->handlers.custom_error == JITErrorBuffer::handler) {
            context->handlers.custom_error = halidoscope_record_error;
        }
        halidoscope_trace_errors.clear();

        {
            TracePipeWriter trace_writer(trace_path, (int64_t)options.trace_file_size_limit * 1000000, context);
            JITSharedRuntime::set_trace_file(trace_writer.fd());
            prepare_trace(traced, trace_target).run(context);
        }

        // The pipe writer swaps in halidoscope_trace_fail when the limit is
        // hit. Its thread has been joined, so this read is safe.
        const bool limit_exceeded = context->handlers.custom_trace == halidoscope_trace_fail;
        if (limit_exceeded || !halidoscope_trace_errors.empty()) {
            ensure_no_file_exists(trace_path);
            scoped_cleanup.run();
        }
        user_assert(!limit_exceeded)
            << "halidoscope: the trace exceeded the size limit of "
            << options.trace_file_size_limit << " MB. Either increase "
            << "HalidoscopeOptions::trace_file_size_limit, or run the pipeline "
            << "on a smaller buffer.\n";
        if (!halidoscope_trace_errors.empty()) {
            halide_runtime_error << halidoscope_trace_errors;
        }
    }

    // --- Profile run: Halide's sampling profiler, captured into JSON. ---
    if (profiling) {
        Pipeline profiled = deserialize_pipeline(data, external_params);
        Target profile_target = base_target.with_feature(Target::Profile);

        {
            HalidoscopeRunner runner = prepare_profile(profiled, profile_target);
            if (profile_runs) {
                for (int i = 0; i < *profile_runs; i++) {
                    std::cout << "Halidoscope profiling run " << i + 1 << " of " << *profile_runs << "\n";
                    runner.run(context);
                }
            } else {
                // The first run includes JIT compilation, so it doesn't count
                // towards the second of profiling.
                runner.run(context);
                auto start = std::chrono::steady_clock::now();
                int runs = 1;
                while (std::chrono::steady_clock::now() - start < std::chrono::seconds(1)) {
                    runner.run(context);
                    runs++;
                }
                std::cout << "Halidoscope ran " << runs << " profiling runs\n";
            }
            using SetJSONOutputFn = void (*)(const char *);
            auto set_json_output = (SetJSONOutputFn)JITSharedRuntime::find_symbol(profile_target, "halide_profiler_set_json_output");
            internal_assert(set_json_output);
            set_json_output(profile_path.c_str());
            // Reports, writing the JSON.
            runner.profiler_scope.reset();
            set_json_output(nullptr);
        }
    }

    // --- Launch Halidoscope, blocking until the window is closed. ---
    std::string binary = halidoscope_path;

    std::vector<std::string> halidoscope_args = {binary, "--trace", trace_path, "--stmt", stmt_path};
    if (profiling) {
        halidoscope_args.emplace_back("--profile");
        halidoscope_args.emplace_back(profile_path);
    }

    int halidoscope_rc = run_process(halidoscope_args);

    // -1 means the binary couldn't be started at all.
    user_assert(halidoscope_rc != -1)
        << "halidoscope: could not find or launch the Halidoscope binary '" << binary
        << "'. Make sure it is installed and on $PATH, or set "
           "HalidoscopeOptions::path to point at it directly.\n";
}

}  // namespace Internal
}  // namespace Halide
