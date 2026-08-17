import { clsx } from "clsx";
import * as d3 from "d3";
import { Separator } from "radix-ui";
import * as React from "react";

import Treemap, {
  type TreemapNode,
} from "@/components/controls/charts/Treemap";
import { useProfileContext } from "@/hooks/profile";
import type { Profile } from "@/types/profile";
import Scatterplot from "@/components/controls/charts/BubbleChart";
import StackedBarChart from "@/components/controls/charts/StackedBarChart";
import ProfilerTable from "@/components/views/profile/ProfilerTable";

interface ProfilePanelProps {
  label: string;
  className?: string;
  contentClassName?: string;
}

function ProfilePanel({
  label,
  className = "",
  contentClassName = "items-center justify-center p-4",
  children,
}: React.PropsWithChildren<ProfilePanelProps>) {
  return (
    <div
      className={clsx(
        "bg-ps-secondary border-ps-titlebar flex min-h-0 basis-1/2 flex-col border-x-3 border-t-0 text-xs last:border-l-0",
        className,
      )}
    >
      <div className="bg-ps-titlebar border-ps-border-primary flex border-r">
        <span className="bg-ps-primary text-ps-text-primary border-ps-border-primary border px-3 py-1 font-semibold">
          {label}
        </span>
      </div>
      <div className={clsx("flex min-h-0 flex-1", contentClassName)}>
        {children}
      </div>
    </div>
  );
}

ProfilePanel.displayName = "Profile.ProfilePanel";

const BYTE_MAGNITUDE_TO_COLOR: Record<string, string> = {
  B: "oklch(0.74 0.175 305.4)",
  KB: "oklch(0.77 0.1919 163.7)",
  MB: "oklch(0.837 0.14 75)",
  GB: "oklch(0.712 0.185 11.3)",
};

function Profile() {
  const { pipelines } = useProfileContext();

  const {
    name,
    runs,
    billed_runs,
    samples,
    time_ns,
    memory_peak,
    memory_total,
    active_threads_numerator,
    active_threads_denominator,
    funcs,
  } = pipelines[0];

  const hierarchy = React.useMemo(() => {
    const bins: TreemapNode[] = [
      { name: "B", children: [] },
      { name: "KB", children: [] },
      { name: "MB", children: [] },
      { name: "GB", children: [] },
    ];
    const k = 1024;

    funcs.forEach((func) => {
      const index = Math.floor(Math.log(func.memory_peak) / Math.log(k));

      if (index >= 0 && index < bins.length) {
        bins[index]?.children?.push({
          name: func.name,
          value: func.memory_peak,
        });
      }
    });

    return {
      name: name,
      children: bins,
    };
  }, [funcs, name]);

  const timings = React.useMemo(
    () =>
      funcs
        .filter((func) => func.time_ns > 0)
        .map((func) => ({
          name: func.name,
          value: func.time_ns,
          kind: func.kind,
        })),
    [funcs],
  );

  const sampledTotal = React.useMemo(
    () => timings.reduce((acc, timing) => acc + timing.value, 0),
    [timings],
  );

  const churn = React.useMemo(() => {
    const k = 1024;
    const bins = ["B", "KB", "MB", "GB"];

    return funcs.map((func) => {
      const index = Math.floor(Math.log(func.memory_peak) / Math.log(k));
      const key = bins[index];

      return {
        x: func.memory_peak,
        y: func.num_allocs,
        r: func.memory_total,
        fill: BYTE_MAGNITUDE_TO_COLOR[key],
        text: func.name,
      };
    });
  }, [funcs]);

  return (
    <div className="flex h-full flex-col">
      <div className="flex flex-1 flex-col overflow-hidden">
        <div className="flex basis-3/5">
          <ProfilePanel label="Peak Memory by Func" className="basis-[60%]">
            <Treemap
              dimensions={{ width: 720, height: 360 }}
              hierarchy={hierarchy}
              rectClassname={(leaf) =>
                clsx("fill-oxide-blue/75 stroke-oxide-blue", {
                  "fill-oxide-red/75! stroke-oxide-red!":
                    leaf.parent?.data.name === "GB",
                  "fill-oxide-yellow/75! stroke-oxide-yellow!":
                    leaf.parent?.data.name === "MB",
                  "fill-oxide-green/75! stroke-oxide-green!":
                    leaf.parent?.data.name === "KB",
                  "fill-oxide-purple/75! stroke-oxide-purple!":
                    leaf.parent?.data.name === "B",
                })
              }
              renderLabel={(d) =>
                d3.format(".2s")(d).replace("k", "K").concat("B")
              }
            />
          </ProfilePanel>
          <ProfilePanel label="Allocation Churn" className="basis-[40%]">
            <Scatterplot
              data={churn}
              dimensions={{ width: 600, height: 400 }}
              labels={{
                x: "Peak Memory",
                y: "Num Allocs",
                r: "Total Memory →",
              }}
              scale="log"
              renderLegendLabel={(d) =>
                d3.format(".2s")(d).replace("k", "K").concat("B")
              }
            />
          </ProfilePanel>
        </div>
        <div className="flex min-h-0 basis-2/5">
          <ProfilePanel
            label="Estimated Share of Run Time (Sampled)"
            className="basis-[60%]"
          >
            <StackedBarChart
              data={timings}
              dimensions={{ width: 720, height: 180 }}
              renderLabel={(d) => (
                <>
                  <span>{d.name}</span>
                  <span>
                    <span className="font-semibold">
                      {(d.value * 1e-6).toFixed(2)}&thinsp;ms
                    </span>{" "}
                    ({((d.value / sampledTotal) * 100).toFixed(1)}%)
                  </span>
                </>
              )}
              barClassname={(d) =>
                d.kind === 0
                  ? undefined
                  : "fill-ps-text-secondary/50! stroke-ps-text-secondary!"
              }
            />
          </ProfilePanel>
          <ProfilePanel
            label="Profiler Output"
            className="basis-[60%]"
            contentClassName="items-start overflow-auto p-0"
          >
            <ProfilerTable pipeline={pipelines[0]} />
          </ProfilePanel>
        </div>
      </div>
      <div className="bg-ps-border-primary text-ps-text-primary border-l-ps-border-primary flex h-8 w-full shrink-0 items-center gap-2 px-8 py-2 text-xs">
        <div className="flex gap-1">
          <span className="text-ps-text-primary/60">Pipeline</span>
          <span className="font-semibold">{name}</span>
        </div>
        <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
        <div className="flex gap-1">
          <span className="text-ps-text-primary/60">Runs</span>
          <span className="font-semibold">{runs}</span>
        </div>
        <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
        <div className="flex gap-1">
          <span className="text-ps-text-primary/60">Billed Runs</span>
          <span className="font-semibold">{billed_runs}</span>
        </div>
        <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
        <div className="flex gap-1">
          <span className="text-ps-text-primary/60">Samples</span>
          <span className="font-semibold">{samples}</span>
        </div>
        <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
        <div className="flex gap-1">
          <span className="text-ps-text-primary/60">Total Time</span>
          <span className="font-semibold">
            {time_ns}&thinsp;ns / {(time_ns * 1e-6).toFixed(3)}&thinsp;ms
          </span>
        </div>
        <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
        <div className="flex gap-1">
          <span className="text-ps-text-primary/60">Peak Memory</span>
          <span className="font-semibold">
            {memory_peak}&thinsp;B / {(memory_peak * 1e-6).toFixed(3)}&thinsp;MB
          </span>
        </div>
        <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
        <div className="flex gap-1">
          <span className="text-ps-text-primary/60">Total Memory</span>
          <span className="font-semibold">
            {memory_total}&thinsp;B / {(memory_total * 1e-6).toFixed(3)}
            &thinsp;MB
          </span>
        </div>
        <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
        <div className="flex gap-1">
          <span className="text-ps-text-primary/60">Avg. Threads</span>
          <span className="font-semibold">
            {(active_threads_numerator / active_threads_denominator).toFixed(3)}
          </span>
        </div>
      </div>
    </div>
  );
}

export default Profile;
