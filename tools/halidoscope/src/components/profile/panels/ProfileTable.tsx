import { clsx } from "clsx";
import * as React from "react";

import { useProfileContext } from "@/hooks/profile";
import type { ProfileFunc } from "@/types/profile";

// Mirrors the `ProfileFunc.kind` tags documented in `@/types/profile`.
const KIND_OVERHEAD = 1;
const KIND_THREAD_IDLE = 2;
const KIND_MALLOC = 3;
const KIND_FREE = 4;
const KIND_ALLOCATION = 7;

// Bit positions in `ProfileFunc.counters_approximated`, matching the
// `counter_*` enum in `profiler_common.cpp`.
const COUNTER_MEMORY_TOTAL = 0;
const COUNTER_NUM_ALLOCS = 1;
const COUNTER_PARALLEL_LOOPS = 2;
const COUNTER_PARALLEL_TASKS = 3;
const COUNTER_POINTS_COMPUTED = 5;

function isApproximate(func: ProfileFunc, counter: number): boolean {
  return ((func.counters_approximated ?? 0) & (1 << counter)) !== 0;
}

/** Marks a conservative upper bound with a leading '<', as the runtime
 * report does. */
function approx(text: string, isApprox: boolean): string {
  return isApprox && text !== "" ? `<${text}` : text;
}

const SI_SUFFIXES = ["", "K", "M", "G", "T", "P", "E"];

/** SI-suffixed byte/allocation counter (10000 -> 10K, 1e6 -> 1.0M, ...),
 * matching Halide's `halide_profiler_report`. Zero renders blank. */
function formatCounter(x: number): string {
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
function formatNormalizedCounter(x: number, runs: number): string {
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
function formatTime(timeNs: number, billedRuns: number): string {
  const runs = billedRuns > 0 ? billedRuns : 1;
  let value = timeNs / (runs * 1e6);
  let unit = "ms";

  if (value >= 1000) {
    value /= 1000;
    unit = "s";
  }

  return `${value.toFixed(2)} ${unit}`;
}

function formatPercent(timeNs: number, pipelineTimeNs: number): string {
  const pct = pipelineTimeNs > 0 ? (timeNs / pipelineTimeNs) * 100 : 0;
  return `(${pct.toFixed(1)}%)`;
}

function formatRecompute(recompute: number | undefined): string {
  if (!recompute || recompute <= 0) {
    return "";
  }
  return recompute >= 10000
    ? formatCounter(Math.round(recompute))
    : recompute.toFixed(2);
}

function formatThreads(numerator: number, denominator: number): string {
  return denominator > 0 ? (numerator / denominator).toFixed(2) : "";
}

interface TreeInfo {
  /** DFS pre-order over the compute_at tree; parent === -1 is a root. */
  order: number[];
  depth: number[];
  isLastSibling: boolean[];
}

function buildTree(funcs: ProfileFunc[]): TreeInfo {
  const n = funcs.length;
  const visited = new Array<boolean>(n).fill(false);
  const depth = new Array<number>(n).fill(0);
  const isLastSibling = new Array<boolean>(n).fill(false);
  const order: number[] = [];

  const dfs = (parentIdx: number, d: number) => {
    let last = -1;
    for (let i = 0; i < n; i++) {
      if (funcs[i].parent === parentIdx && !visited[i]) {
        last = i;
      }
    }
    for (let i = 0; i < n; i++) {
      if (funcs[i].parent === parentIdx && !visited[i]) {
        visited[i] = true;
        depth[i] = d;
        isLastSibling[i] = i === last;
        order.push(i);
        dfs(i, d + 1);
      }
    }
  };
  dfs(-1, 0);

  // Orphans (parent points outside the array) get appended at depth 0
  // rather than being silently dropped.
  for (let i = 0; i < n; i++) {
    if (!visited[i]) {
      depth[i] = 0;
      isLastSibling[i] = true;
      order.push(i);
    }
  }

  return { order, depth, isLastSibling };
}

interface CumulativeStats {
  time_ns: number;
  active_threads_numerator: number;
  active_threads_denominator: number;
}

/** Rolls each Func's time and active-thread stats up into its ancestors, for
 * the cumulative "active threads" column. Used for profiles that predate the
 * per-Func `cumulative` field. */
function computeCumulativeStats(
  funcs: ProfileFunc[],
  order: number[],
): CumulativeStats[] {
  const cumulative: CumulativeStats[] = funcs.map(() => ({
    time_ns: 0,
    active_threads_numerator: 0,
    active_threads_denominator: 0,
  }));

  // Descendants always sort after their ancestor in `order`, so walking it
  // backwards guarantees a node's children are folded in before its own
  // total is propagated up to its parent.
  for (let i = order.length - 1; i >= 0; i--) {
    const j = order[i];
    const cs = cumulative[j];
    cs.time_ns += funcs[j].time_ns;
    cs.active_threads_numerator += funcs[j].active_threads_numerator;
    cs.active_threads_denominator += funcs[j].active_threads_denominator;

    const parent = funcs[j].parent;
    if (parent >= 0) {
      const parentCs = cumulative[parent];
      parentCs.time_ns += cs.time_ns;
      parentCs.active_threads_numerator += cs.active_threads_numerator;
      parentCs.active_threads_denominator += cs.active_threads_denominator;
    }
  }

  return cumulative;
}

interface NameCellProps {
  func: ProfileFunc;
  funcs: ProfileFunc[];
  idx: number;
  depth: number;
  isLastSibling: boolean[];
}

/** Renders a Func's name with tree-art indentation: `│` continues an
 * ancestor's subtree, `├`/`└` connects this row to its parent. */
function NameCell({ func, funcs, idx, depth, isLastSibling }: NameCellProps) {
  if (depth === 0) {
    return <span>{func.name}</span>;
  }

  const lineage: number[] = [];
  let j = idx;
  for (let k = depth; k > 0; k--) {
    lineage[k - 1] = j;
    j = funcs[j].parent;
  }

  return (
    <span className="whitespace-pre">
      <span className="text-ps-text-secondary/50">
        {lineage
          .slice(0, depth - 1)
          .map((ancestor) => (isLastSibling[ancestor] ? " " : "│"))
          .join("")}
        {isLastSibling[lineage[depth - 1]] ? "└" : "├"}
      </span>
      {func.name}
    </span>
  );
}

interface HeaderCellProps {
  label: string;
  className?: string;
}

function HeaderCell({ label, className }: HeaderCellProps) {
  return (
    <th
      className={clsx(
        "text-ps-text-primary/60 px-2 py-0.5 font-normal italic",
        className,
      )}
    >
      <div>{label}</div>
    </th>
  );
}

/**
 * Renders the same per-Func breakdown as Halide's sampling profiler prints
 * to stdout (see `halide_profiler_report_unlocked` in `profiler_common.cpp`),
 * as an HTML table.
 */
function ProfileTable() {
  const { pipelines } = useProfileContext();
  const {
    funcs,
    runs,
    billed_runs,
    time_ns,
    num_allocs,
    warnings: pipelineWarnings = [],
  } = pipelines[0];

  const { order, depth, isLastSibling } = React.useMemo(
    () => buildTree(funcs),
    [funcs],
  );

  const cumulative = React.useMemo(() => {
    const computed = computeCumulativeStats(funcs, order);
    return funcs.map((fs, i) => fs.cumulative ?? computed[i]);
  }, [funcs, order]);

  const rows = React.useMemo(
    () =>
      order.filter((i) => {
        const fs = funcs[i];
        if (
          (fs.kind === KIND_OVERHEAD || fs.kind === KIND_THREAD_IDLE) &&
          fs.time_ns === 0
        ) {
          return false;
        }
        if (
          (fs.kind === KIND_MALLOC || fs.kind === KIND_FREE) &&
          num_allocs === 0
        ) {
          return false;
        }
        return true;
      }),
    [funcs, num_allocs, order],
  );

  // Number the per-Func warnings in table order. Instances of a Func share a
  // canonical id and therefore the same warnings and numbers.
  const { warningList, notes } = React.useMemo(() => {
    const warningList: string[] = [];
    const byCanonical = new Map<number, number[]>();
    for (const i of rows) {
      const fs = funcs[i];
      if (!fs.warnings?.length || byCanonical.has(fs.canonical_id)) {
        continue;
      }
      byCanonical.set(
        fs.canonical_id,
        fs.warnings.map((w) => warningList.push(w)),
      );
    }
    const notes = funcs.map((fs) =>
      (byCanonical.get(fs.canonical_id) ?? []).join(","),
    );
    return { warningList, notes };
  }, [funcs, rows]);

  return (
    <div className="text-ps-text-primary text-tiny m-4 font-mono">
      <table className="w-full border-collapse">
        <thead className="bg-ps-secondary sticky top-0">
          <tr className="border-ps-border-tertiary border-b text-left">
            <HeaderCell label="name" />
            <HeaderCell label="time" className="text-right" />
            <HeaderCell label="percent" className="text-right" />
            <HeaderCell label="active threads" className="text-right" />
            <HeaderCell label="parallel loops" className="text-right" />
            <HeaderCell label="parallel tasks" className="text-right" />
            <HeaderCell label="heap allocs" className="text-right" />
            <HeaderCell label="peak mem" className="text-right" />
            <HeaderCell label="avg mem" className="text-right" />
            <HeaderCell label="recompute ratio" className="text-right" />
            <HeaderCell label="notes" />
          </tr>
        </thead>
        <tbody>
          {rows.map((i) => {
            const fs = funcs[i];
            const cs = cumulative[i];
            const isAllocation = fs.kind === KIND_ALLOCATION;
            const peakMem = fs.num_allocs > 0 ? fs.memory_peak : fs.stack_peak;
            const avgMem =
              fs.num_allocs > 0
                ? Math.floor(fs.memory_total / fs.num_allocs)
                : 0;

            return (
              <tr key={i} className="hover:bg-ps-border-primary/40">
                <td className="px-2 py-0.5">
                  <NameCell
                    func={fs}
                    funcs={funcs}
                    idx={i}
                    depth={depth[i]}
                    isLastSibling={isLastSibling}
                  />
                </td>
                {isAllocation ? (
                  <td
                    colSpan={2}
                    className="text-ps-text-secondary px-2 py-0.5 text-center italic"
                  >
                    (allocation)
                  </td>
                ) : (
                  <>
                    <td className="px-2 py-0.5 text-right">
                      {formatTime(fs.time_ns, billed_runs)}
                    </td>
                    <td className="px-2 py-0.5 text-right">
                      {formatPercent(fs.time_ns, time_ns)}
                    </td>
                  </>
                )}
                <td className="px-2 py-0.5 text-right">
                  {!isAllocation && cs.time_ns > 0
                    ? formatThreads(
                        cs.active_threads_numerator,
                        cs.active_threads_denominator,
                      )
                    : ""}
                </td>
                <td className="px-2 py-0.5 text-right">
                  {isAllocation
                    ? ""
                    : approx(
                        formatNormalizedCounter(fs.parallel_loops, runs),
                        isApproximate(fs, COUNTER_PARALLEL_LOOPS),
                      )}
                </td>
                <td className="px-2 py-0.5 text-right">
                  {isAllocation
                    ? ""
                    : approx(
                        formatNormalizedCounter(fs.parallel_tasks, runs),
                        isApproximate(fs, COUNTER_PARALLEL_TASKS),
                      )}
                </td>
                <td className="px-2 py-0.5 text-right">
                  {approx(
                    formatNormalizedCounter(fs.num_allocs, runs),
                    isApproximate(fs, COUNTER_NUM_ALLOCS),
                  )}
                </td>
                <td className="px-2 py-0.5 text-right">
                  {formatCounter(peakMem)}
                </td>
                <td className="px-2 py-0.5 text-right">
                  {approx(
                    formatCounter(avgMem),
                    isApproximate(fs, COUNTER_MEMORY_TOTAL),
                  )}
                </td>
                <td className="px-2 py-0.5 text-right">
                  {isAllocation
                    ? ""
                    : approx(
                        formatRecompute(fs.recompute),
                        isApproximate(fs, COUNTER_POINTS_COMPUTED),
                      )}
                </td>
                <td className="px-2 py-0.5">{isAllocation ? "" : notes[i]}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
      {(pipelineWarnings.length > 0 || warningList.length > 0) && (
        <div className="mt-3 font-sans">
          <div className="text-ps-text-primary/60 italic">
            Performance warnings:
          </div>
          <ul className="ml-4 list-disc">
            {pipelineWarnings.map((w, k) => (
              <li key={k}>{w}</li>
            ))}
          </ul>
          <ol className="ml-4 list-decimal">
            {warningList.map((w, k) => (
              <li key={k}>{w}</li>
            ))}
          </ol>
        </div>
      )}
    </div>
  );
}

export default ProfileTable;
