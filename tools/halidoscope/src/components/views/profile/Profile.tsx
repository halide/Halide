import { clsx } from "clsx";
import { Separator } from "radix-ui";
import * as React from "react";

import Treemap, {
  type TreemapNode,
} from "@/components/controls/charts/Treemap";
import { useProfileContext } from "@/hooks/profile";
import type { Profile } from "@/types/profile";
import BubbleChart from "@/components/controls/charts/BubbleChart";
import StackedBarChart from "@/components/controls/charts/StackedBarChart";
import ByteUnit from "@/components/shared/ByteUnit";
import ProfilerTable from "@/components/views/profile/ProfilerTable";
import { formatBytes } from "@/utils/formatters";

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

function ProfileOverview() {
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
  } = pipelines[0];

  return (
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
        <span className="text-ps-text-primary/60">Avg. Time / Run</span>
        <span className="font-semibold">
          {time_ns / billed_runs}&thinsp;ns /{" "}
          {((time_ns / billed_runs) * 1e-6).toFixed(3)}&thinsp;ms
        </span>
      </div>
      <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
      <div className="flex gap-1">
        <span className="text-ps-text-primary/60">Peak Memory</span>
        <span className="font-semibold">
          {memory_peak}&thinsp;
          <span style={{ color: BYTE_MAGNITUDE_TO_COLOR["B"] }}>B</span> /{" "}
          {(memory_peak * 1e-6).toFixed(3)}&thinsp;
          <span style={{ color: BYTE_MAGNITUDE_TO_COLOR["MB"] }}>MB</span>
        </span>
      </div>
      <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
      <div className="flex gap-1">
        <span className="text-ps-text-primary/60">Total Memory</span>
        <span className="font-semibold">
          <span className="font-semibold">
            {memory_total / runs}&thinsp;
            <span style={{ color: BYTE_MAGNITUDE_TO_COLOR["B"] }}>
              B
            </span> / {((memory_total / runs) * 1e-6).toFixed(3)}&thinsp;
            <span style={{ color: BYTE_MAGNITUDE_TO_COLOR["MB"] }}>MB</span>
          </span>
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
  );
}

ProfileOverview.displayName = "Profile.ProfileOverview";

const BYTE_MAGNITUDE_TO_COLOR: Record<string, string> = {
  B: "oklch(0.74 0.175 305.4)",
  KB: "oklch(0.77 0.1919 163.7)",
  MB: "oklch(0.837 0.14 75)",
  GB: "oklch(0.712 0.185 11.3)",
};

function Profile() {
  const { pipelines } = useProfileContext();

  const { name, runs, billed_runs, funcs } = pipelines[0];

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
          value: func.time_ns / billed_runs,
          kind: func.kind,
        })),
    [billed_runs, funcs],
  );

  const sampledTotal = React.useMemo(
    () => timings.reduce((acc, timing) => acc + timing.value / billed_runs, 0),
    [billed_runs, timings],
  );

  const churn = React.useMemo(() => {
    const k = 1024;
    const bins = ["B", "KB", "MB", "GB"];

    return funcs
      .map((func) => {
        const index = Math.floor(Math.log(func.memory_peak) / Math.log(k));
        const key = bins[index];

        return {
          id: func.name,
          x: func.memory_peak,
          y: func.num_allocs / runs,
          r: func.memory_total / runs,
          color: BYTE_MAGNITUDE_TO_COLOR[key],
        };
      })
      .filter((d) => d.x > 0 && d.y > 0);
  }, [funcs, runs]);

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
              renderLabel={(d) => {
                const { value, unit } = formatBytes(d);

                return `${value}\u2009${unit}`;
              }}
            />
          </ProfilePanel>
          <ProfilePanel label="Allocation Churn" className="basis-[40%]">
            <BubbleChart
              data={churn}
              dimensions={{ width: 600, height: 400 }}
              labels={{
                x: "Peak Memory",
                y: "Num Allocs",
                r: "Total Memory →",
              }}
              renderLegendLabel={(d) => {
                const { value, unit } = formatBytes(d);

                return `${value}\u2009${unit}`;
              }}
              renderTooltip={({ id, x, y, r }) => (
                <div className="flex flex-col items-center gap-1.5">
                  <p>{id}</p>
                  <div className="grid grid-cols-2 gap-x-0.5 gap-y-px">
                    <p>Peak Memory</p>
                    <ByteUnit value={x} className="justify-self-end" />
                    <p>Total Memory</p>
                    <ByteUnit value={r} className="justify-self-end" />
                    <p>Num Allocs</p>
                    <p className="text-right font-semibold">{y}</p>
                  </div>
                </div>
              )}
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
                    <span
                      className={clsx("font-semibold", {
                        "text-ps-text-primary": d.kind !== 0,
                      })}
                    >
                      {(d.value * 1e-6).toFixed(2)}&thinsp;ms
                    </span>{" "}
                    ({((d.value / billed_runs / sampledTotal) * 100).toFixed(1)}
                    %)
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
      <ProfileOverview />
    </div>
  );
}

export default Profile;
