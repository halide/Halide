import { useAtom } from "jotai";
import { Accordion } from "radix-ui";

import * as React from "react";

import { useOpenFuncMenu } from "@/hooks/funcMenu";
import { useProfileContext } from "@/hooks/profile";
import { funcAtom } from "@/state/func";
import type { ProfileFunc, ProfilePipeline } from "@/types/profile";
import type { FuncMeta } from "@/types/trace";
import {
  approx,
  COUNTER_MEMORY_TOTAL,
  COUNTER_NUM_ALLOCS,
  COUNTER_PARALLEL_LOOPS,
  COUNTER_PARALLEL_TASKS,
  COUNTER_POINTS_COMPUTED,
  formatCounter,
  formatNormalizedCounter,
  formatPercent,
  formatRecompute,
  formatThreads,
  formatTime,
  isApproximate,
  KIND_ALLOCATION,
  KIND_FUNC,
} from "@/utils/profile";

interface Props {
  funcs: Record<string, FuncMeta>;
}

function Field({ label, value }: { label: string; value: React.ReactNode }) {
  return (
    <>
      <span className="text-ps-text-secondary bg-ps-border-primary p-1 font-sans font-semibold">
        {label}
      </span>
      <span className="bg-ps-border-primary p-1 font-mono">{value}</span>
    </>
  );
}

/** The sampling profiler's stats for one profile entry of a Func, as in the profile table. */
function ProfileFields({
  func: fs,
  pipeline,
  title,
}: {
  func: ProfileFunc;
  pipeline: ProfilePipeline;
  title: string;
}) {
  const { runs, billed_runs, time_ns } = pipeline;
  const isAllocation = fs.kind === KIND_ALLOCATION;
  const cs = fs.cumulative ?? fs;
  const peakMem = fs.num_allocs > 0 ? fs.memory_peak : fs.stack_peak;
  const avgMem =
    fs.num_allocs > 0 ? Math.floor(fs.memory_total / fs.num_allocs) : 0;

  return (
    <>
      <span className="text-ps-text-primary bg-ps-border-primary col-span-2 p-1 font-sans font-semibold">
        {title}
      </span>
      {isAllocation ? null : (
        <>
          <Field
            label="Time"
            value={`${formatTime(fs.time_ns, billed_runs)} ${formatPercent(fs.time_ns, time_ns)}`}
          />
          <Field
            label="Active Threads"
            value={
              cs.time_ns > 0
                ? formatThreads(
                    cs.active_threads_numerator,
                    cs.active_threads_denominator,
                  )
                : ""
            }
          />
          <Field
            label="Parallel Loops"
            value={approx(
              formatNormalizedCounter(fs.parallel_loops, runs),
              isApproximate(fs, COUNTER_PARALLEL_LOOPS),
            )}
          />
          <Field
            label="Parallel Tasks"
            value={approx(
              formatNormalizedCounter(fs.parallel_tasks, runs),
              isApproximate(fs, COUNTER_PARALLEL_TASKS),
            )}
          />
        </>
      )}
      <Field
        label="Heap Allocations"
        value={approx(
          formatNormalizedCounter(fs.num_allocs, runs),
          isApproximate(fs, COUNTER_NUM_ALLOCS),
        )}
      />
      <Field label="Peak Memory" value={formatCounter(peakMem)} />
      <Field
        label="Average Memory"
        value={approx(
          formatCounter(avgMem),
          isApproximate(fs, COUNTER_MEMORY_TOTAL),
        )}
      />
      {isAllocation ? null : (
        <Field
          label="Recompute Ratio"
          value={approx(
            formatRecompute(fs.recompute),
            isApproximate(fs, COUNTER_POINTS_COMPUTED),
          )}
        />
      )}
      {fs.warnings?.map((w, k) => (
        <span
          key={k}
          className="bg-ps-border-primary text-oxide-yellow col-span-2 p-1 font-sans"
        >
          {w}
        </span>
      ))}
    </>
  );
}

function FuncsPanel({ funcs }: Props) {
  const [func, setFunc] = useAtom(funcAtom);
  const openMenu = useOpenFuncMenu();
  const pipeline: ProfilePipeline | undefined =
    useProfileContext().pipelines[0];

  return (
    <Accordion.Root
      type="single"
      collapsible
      className="flex w-full flex-col px-3 py-2 text-xs"
      value={func}
      onValueChange={(value) => setFunc(value)}
    >
      {Object.values(funcs).map((func) => (
        <Accordion.Item
          key={func.name}
          value={func.name}
          className="group flex flex-col"
        >
          <Accordion.Trigger
            className="flex w-full items-baseline gap-1 py-2 text-left font-mono"
            onContextMenu={(event) => openMenu(event, func.name)}
          >
            <svg
              className="transition-transform duration-200 group-data-[state=open]:rotate-90"
              width="8"
              height="8"
              viewBox="0 0 12 12"
              fill="currentColor"
            >
              <polygon points="0,0 8,6 0,12" />
            </svg>
            {func.name}
          </Accordion.Trigger>
          <Accordion.Content className="accordion-content ml-3 overflow-hidden">
            <div className="text-tiny bg-ps-border-tertiary border-ps-border-tertiary grid grid-cols-2 gap-y-px border">
              <Field
                label="Minimum Coordinates"
                value={`(${func.min_coords.join(",")})`}
              />
              <Field
                label="Maximum Coordinates"
                value={`(${func.max_coords.join(",")})`}
              />
              <Field label="Minimum Value" value={func.min_value} />
              <Field label="Maximum Value" value={func.max_value} />
              <Field
                label="Maximum Store Count"
                value={func.max_store_count.toLocaleString()}
              />
              <Field
                label="Maximum Load Count"
                value={func.max_load_count.toLocaleString()}
              />
              <Field
                label="Thread Count"
                value={func.thread_count.toLocaleString()}
              />
              {pipeline
                ? pipeline.funcs
                    .filter(
                      (fs) =>
                        (fs.kind === KIND_FUNC ||
                          fs.kind === KIND_ALLOCATION) &&
                        fs.ir_name === func.name,
                    )
                    .map((fs, k, entries) => (
                      <ProfileFields
                        key={k}
                        func={fs}
                        pipeline={pipeline}
                        title={
                          fs.kind === KIND_ALLOCATION
                            ? "Profile (allocation)"
                            : entries.length > 1
                              ? `Profile (instance ${k + 1})`
                              : "Profile"
                        }
                      />
                    ))
                : null}
            </div>
          </Accordion.Content>
        </Accordion.Item>
      ))}
    </Accordion.Root>
  );
}

export default FuncsPanel;
