import type { ProfileFunc, ProfilePipeline } from "@/types/profile";

export const KIND_FUNC = 0;
export const KIND_OVERHEAD = 1;
export const KIND_THREAD_IDLE = 2;
export const KIND_MALLOC = 3;
export const KIND_FREE = 4;
export const KIND_COPY_TO_HOST = 5;
export const KIND_COPY_TO_DEVICE = 6;
export const KIND_ALLOCATION = 7;

/**
 * The IR name of the Func a profile entry belongs to, or null for
 * bookkeeping entries.
 */
export function profileEntryFunc(
  func: ProfileFunc,
  funcs: ProfileFunc[],
): string | null {
  switch (func.kind) {
    case KIND_FUNC:
    case KIND_ALLOCATION:
      return func.ir_name;
    case KIND_COPY_TO_HOST:
    case KIND_COPY_TO_DEVICE:
      return funcs[func.buffer_func_id]?.ir_name ?? null;
    default:
      return null;
  }
}

/** The canonical id of the Func with the given IR name, or null. */
export function profileFuncId(
  pipeline: ProfilePipeline,
  irName: string,
): number | null {
  const func = pipeline.funcs.find(
    (f) =>
      (f.kind === KIND_FUNC || f.kind === KIND_ALLOCATION) &&
      f.ir_name === irName,
  );
  return func?.canonical_id ?? null;
}

// Bit positions in `ProfileFunc.counters_approximated`, matching the
// `counter_*` enum in `profiler_common.cpp`.
export const COUNTER_MEMORY_TOTAL = 0;
export const COUNTER_NUM_ALLOCS = 1;
export const COUNTER_PARALLEL_LOOPS = 2;
export const COUNTER_PARALLEL_TASKS = 3;
export const COUNTER_POINTS_COMPUTED = 5;

export function isApproximate(func: ProfileFunc, counter: number): boolean {
  return ((func.counters_approximated ?? 0) & (1 << counter)) !== 0;
}

/** Marks a conservative upper bound with a leading '<', as the runtime
 * report does. */
export function approx(text: string, isApprox: boolean): string {
  return isApprox && text !== "" ? `<${text}` : text;
}

const SI_SUFFIXES = ["", "K", "M", "G", "T", "P", "E"];

/** SI-suffixed byte/allocation counter (10000 -> 10K, 1e6 -> 1.0M, ...),
 * matching Halide's `halide_profiler_report`. Zero renders blank. */
export function formatCounter(x: number): string {
  if (x <= 0) {
    return "";
  }

  let value = x;
  let scale = 0;
  while (value >= 10000) {
    scale++;
    value = Math.floor((value + 499) / 1000);
  }

  return `${value}${SI_SUFFIXES[scale]}`;
}

/** A counter accumulated over `runs` runs. Renders the per-run value if
 * constant per run, otherwise the average. Zero renders blank. */
export function formatNormalizedCounter(x: number, runs: number): string {
  if (x <= 0) {
    return "";
  }
  if (runs <= 0) {
    return formatCounter(x);
  }
  if (x % runs === 0) {
    return formatCounter(x / runs);
  }

  const avg = x / runs;
  return avg >= 10000 ? formatCounter(Math.round(avg)) : avg.toFixed(2);
}

/** Time billed to a Func, averaged over the runs the sampler reached. */
export function formatTime(timeNs: number, billedRuns: number): string {
  const runs = billedRuns > 0 ? billedRuns : 1;
  let value = timeNs / (runs * 1e6);
  let unit = "ms";

  if (value >= 1000) {
    value /= 1000;
    unit = "s";
  }

  return `${value.toFixed(2)} ${unit}`;
}

export function formatPercent(timeNs: number, pipelineTimeNs: number): string {
  const pct = pipelineTimeNs > 0 ? (timeNs / pipelineTimeNs) * 100 : 0;
  return `(${pct.toFixed(1)}%)`;
}

export function formatRecompute(recompute: number | undefined): string {
  if (!recompute || recompute <= 0) {
    return "";
  }
  return recompute >= 10000
    ? formatCounter(Math.round(recompute))
    : recompute.toFixed(2);
}

export function formatThreads(numerator: number, denominator: number): string {
  return denominator > 0 ? (numerator / denominator).toFixed(2) : "";
}
