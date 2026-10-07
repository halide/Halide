import { Separator } from "radix-ui";
import * as React from "react";

import { useProfileContext } from "@/hooks/profile";
import { formatBytes, formatTime } from "@/utils/formatters";

const perRun = (x: number, runs: number) => (runs > 0 ? x / runs : 0);

function ProfileOverview() {
  const { pipelines } = useProfileContext();

  const {
    name,
    runs,
    billed_runs,
    samples,
    time_ns,
    num_allocs,
    memory_peak,
    memory_total,
    active_threads_numerator,
    active_threads_denominator,
  } = pipelines[0];

  const stats: [string, string][] = [
    ["Pipeline", name],
    ["Time / run", formatTime(perRun(time_ns, billed_runs))],
    [
      "Avg. threads",
      active_threads_denominator > 0
        ? (active_threads_numerator / active_threads_denominator).toFixed(2)
        : "-",
    ],
    ["Peak heap", formatBytes(memory_peak)],
    ["Heap allocated / run", formatBytes(perRun(memory_total, runs))],
    ["Heap allocs / run", perRun(num_allocs, runs).toLocaleString()],
    ["Runs", runs.toLocaleString()],
    ["Billed runs", billed_runs.toLocaleString()],
    ["Samples", samples.toLocaleString()],
  ];

  return (
    <div className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary flex w-full shrink-0 flex-wrap items-stretch gap-4 border-b px-6 py-2">
      {stats.map(([label, value], i) => (
        <React.Fragment key={label}>
          {i > 0 ? (
            <Separator.Root
              orientation="vertical"
              className="bg-ps-border-tertiary w-px"
            />
          ) : null}
          <div className="flex flex-col">
            <span className="text-ps-text-primary/60 text-xs">{label}</span>
            <span className="text-base font-semibold">{value}</span>
          </div>
        </React.Fragment>
      ))}
    </div>
  );
}

export default ProfileOverview;
