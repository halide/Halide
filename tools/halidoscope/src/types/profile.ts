/**
 * Represents per-Func profiling stats for a single run of a pipeline, as
 * captured by Halide's sampling profiler.
 */
export interface ProfileFunc {
  /** The name of the Func. */
  name: string;
  /** The id of the parent Func this one is `compute_at`. `-1` if the Func is
   * `compute_root`.
   */
  parent: number;
  /**
   * The id of this Func's canonical entry.
   *
   * @remarks
   *
   * A Func can appear in the funcs array more than once (e.g., an unscheduled
   * Func with an update definition reached from multiple callers);
   * `canonical_id` is the id of the first such appearance, the shared key for
   * rolling instances back up to a single Func.
   */
  canonical_id: number;
  /**
   * A tag identifying what this entry represents.
   *
   * @remarks
   *
   * `0` = an ordinary Func,
   * `1` = profiler overhead bookkeeping,
   * `2` = thread-idle bookkeeping,
   * `3` = malloc,
   * `4` = free,
   * `5` = `copy_to_host`,
   * `6` = `copy_to_device`,
   * `7` = a `hoist_storage` allocation entry (carries the memory columns for
   * the buffer's lifetime, while the time/compute columns belong to a separate
   * production entry).
   */
  kind: 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7;
  /**
   * For `copy_to_host`/`copy_to_device` entries (see {@link ProfileFunc.kind}),
   * the `canonical_id` of the Func whose buffer is being copied. `-1` otherwise.
   */
  buffer_func_id: number;
  /** The total time spent evaluating this Func, in nanoseconds. */
  time_ns: number;
  /** The current memory allocation of this Func, in bytes. */
  memory_current: number;
  /** The peak memory allocation of this Func, in bytes. */
  memory_peak: number;
  /** The total memory allocation of this Func, in bytes. */
  memory_total: number;
  /** The peak stack allocation across this Func's threads, in bytes. */
  stack_peak: number;
  /**
   * The numerator of the average number of thread pool worker threads active
   * while computing this Func. Divide by {@link ProfileFunc.active_threads_denominator}
   * to get the average.
   */
  active_threads_numerator: number;
  /**
   * The denominator of the average number of thread pool worker threads active
   * while computing this Func.
   */
  active_threads_denominator: number;
  /** The total number of times heap storage for this Func was allocated. */
  num_allocs: number;
}

/**
 * Represents profiling stats for a single run of a pipeline, as captured by
 * Halide's sampling profiler.
 */
export interface ProfilePipeline {
  /** The name of the pipeline. */
  name: string;
  /** The number of times this pipeline has been run, ever, since the last reset. */
  runs: number;
  /**
   * The number of pipeline runs that produced at least one profiler sample.
   *
   * @remarks
   *
   * Runs that completed in less than one sampler tick contribute to
   * {@link ProfilePipeline.runs} (and to the per-Func counters) but not to
   * per-Func time accumulation, so this is the correct denominator for time
   * averages.
   */
  billed_runs: number;
  /** The total number of samples taken inside this pipeline, across all its runs. */
  samples: number;
  /** The total number of memory allocations made by Funcs in this pipeline,across all runs. */
  num_allocs: number;
  /**
   * The time billed to Funcs in this pipeline run by the sampling thread,
   * in nanoseconds.
   */
  time_ns: number;
  /** The current memory allocation of Funcs in this pipeline run, in bytes. */
  memory_current: number;
  /** The peak memory allocation of Funcs in this pipeline run, in bytes. */
  memory_peak: number;
  /** The total memory allocation of Funcs in this pipeline run, in bytes. */
  memory_total: number;
  /**
   * The numerator of the average number of thread pool worker threads doing
   * useful work while computing this pipeline run. Divide by
   * {@link ProfilePipeline.active_threads_denominator} to get the average.
   */
  active_threads_numerator: number;
  /**
   * The denominator of the average number of thread pool worker threads doing
   * useful work while computing this pipeline run.
   */
  active_threads_denominator: number;
  /** Per-Func profiling stats for this pipeline run. */
  funcs: ProfileFunc[];
}

/**
 * Represents the complete profiling payload for a pipeline run, as returned by
 * the `Pipeline::halidoscope` capture or from a pre-recorded JSON file.
 */
export interface Profile {
  /** The profiled pipelines captured in this profile snapshot. */
  pipelines: ProfilePipeline[];
}
