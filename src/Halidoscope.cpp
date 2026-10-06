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

#include <zstd.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Halide {

using namespace Internal;

namespace {

// Streams bytes through zstd into a file.
class ZstdFileWriter {
    std::ofstream out;
    ZSTD_CCtx *cctx = nullptr;
    std::vector<char> out_buf;
    int64_t limit_bytes;

    bool pump(ZSTD_inBuffer &in, ZSTD_EndDirective mode) {
        bool done = false;
        while (!done && limit_bytes >= 0) {
            ZSTD_outBuffer o = {out_buf.data(), out_buf.size(), 0};
            size_t remaining = ZSTD_compressStream2(cctx, &o, &in, mode);
            user_assert(!ZSTD_isError(remaining))
                << "halidoscope: zstd compression failed: " << ZSTD_getErrorName(remaining) << "\n";
            out.write(out_buf.data(), o.pos);
            limit_bytes -= o.pos;
            done = (mode == ZSTD_e_end) ? remaining == 0 : in.pos == in.size;
        }
        return limit_bytes >= 0;
    }

public:
    explicit ZstdFileWriter(const std::string &path, int64_t limit_bytes)
        : out(path, std::ios::binary), cctx(ZSTD_createCCtx()), out_buf(ZSTD_CStreamOutSize()), limit_bytes(limit_bytes) {
        user_assert(out.good()) << "halidoscope: unable to open " << path << " for writing\n";
        ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, ZSTD_CLEVEL_DEFAULT);
        // Compress on background threads if this libzstd supports it.
        ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, 4);
    }

    ~ZstdFileWriter() {
        ZSTD_freeCCtx(cctx);
    }

    ZstdFileWriter(const ZstdFileWriter &) = delete;
    ZstdFileWriter &operator=(const ZstdFileWriter &) = delete;

    bool write(const void *data, size_t size) {
        ZSTD_inBuffer in = {data, size, 0};
        return pump(in, ZSTD_e_continue);
    }

    void finish() {
        ZSTD_inBuffer in = {nullptr, 0, 0};
        pump(in, ZSTD_e_end);
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

// Compresses everything written to the returned file descriptor into path
// on a background thread, until the descriptor is closed.
class ZstdPipeWriter {
    int read_fd = -1, write_fd = -1;
    ZstdFileWriter writer;
    std::thread thread;
    JITUserContext *user_context = nullptr;

public:
    explicit ZstdPipeWriter(const std::string &path, int64_t limit_bytes, JITUserContext *user_context)
        : writer(path, limit_bytes), user_context(user_context) {
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

    ZstdPipeWriter(const ZstdPipeWriter &) = delete;
    ZstdPipeWriter &operator=(const ZstdPipeWriter &) = delete;

    int fd() const {
        return write_fd;
    }

    ~ZstdPipeWriter() {
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
                          const std::function<HalidoscopeRunner(Pipeline &, const Target &)> &prepare,
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

    struct ScopedCleanup {
        bool armed;
        std::string trace_path, profile_path, dir;
        ScopedCleanup(bool armed, const std::string &tp, const std::string &pp, const std::string &d)
            : armed(armed), trace_path(tp), profile_path(pp), dir(d) {
        }
        void run() {
            if (armed) {
                ensure_no_file_exists(trace_path);
                ensure_no_file_exists(profile_path);
                dir_rmdir(dir);
                armed = false;
            }
        }
        ~ScopedCleanup() {
            run();
        }
    } scoped_cleanup(!options.output_dir, trace_path, profile_path, dir);

    // --- Trace run: every Func's loads/stores/realizations, dumped to a
    // zstd-compressed binary trace file. ---
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
            ZstdPipeWriter trace_writer(trace_path, (int64_t)options.trace_file_size_limit * 1000000, context);
            JITSharedRuntime::set_trace_file(trace_writer.fd());
            prepare(traced, trace_target).run(context);
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
            HalidoscopeRunner runner = prepare(profiled, profile_target);
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

    std::vector<std::string> halidoscope_args = {binary, "--trace", trace_path};
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
