#ifndef HALIDE_HALIDOSCOPE_H
#define HALIDE_HALIDOSCOPE_H

/** \file
 *
 * Support for Pipeline::halidoscope() and GeneratorBase::halidoscope(), which
 * run a pipeline under tracing and profiling and open the results in the
 * Halidoscope GUI (tools/halidoscope).
 */

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "Target.h"

namespace Halide {

class Pipeline;
class ProfilerScope;
struct JITUserContext;

/** Options for Pipeline::halidoscope() and GeneratorBase::halidoscope(). */
struct HalidoscopeOptions {
    /** (Optional) Path to the halidoscope executable, or just its name if
     * it's on $PATH. If unset, defaults to looking up "halidoscope" on
     * $PATH. */
    std::optional<std::string> path = std::nullopt;
    /** (Optional) Path to the non-volatile directory for storing
     * Halidoscope-generated trace binaries and profiler output. */
    std::optional<std::string> output_dir = std::nullopt;
    /** (Optional) The number of runs for the profiler execution. If unset,
     * the pipeline is run repeatedly for at least one second, not counting
     * the first run (which includes JIT compilation). A 0 value indicates
     * that profiling should be skipped. */
    std::optional<int> profile_runs = std::nullopt;
    /** (Non-optional) A limit on the size of the trace file in megabytes, to
     * avoid accidentally filling the disk with a massive trace. Defaults to 1
     * gigabyte. */
    size_t trace_file_size_limit = 1000;
};

namespace Internal {

/** An instrumented pipeline, compiled for some Target. */
struct HalidoscopeRunner {
    /** Runs the pipeline once. */
    std::function<void(JITUserContext *)> run;
    /** Accumulates profiling statistics across calls to run. */
    std::unique_ptr<ProfilerScope> profiler_scope;
};

using HalidoscopePrepareFn = std::function<HalidoscopeRunner(Pipeline &, const Target &)>;

/** The implementation of Pipeline::halidoscope() and
 * GeneratorBase::halidoscope(). prepare_trace and prepare_profile compile an
 * instrumented copy of the pipeline for a Target, for the tracing run and the
 * profiling runs respectively. */
void pipeline_halidoscope(const Pipeline &pipeline,
                          JITUserContext *context,
                          const HalidoscopePrepareFn &prepare_trace,
                          const HalidoscopePrepareFn &prepare_profile,
                          const HalidoscopeOptions &options,
                          const Target &target);

}  // namespace Internal
}  // namespace Halide

#endif
