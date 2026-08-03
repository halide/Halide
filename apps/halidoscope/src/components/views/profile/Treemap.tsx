import { clsx } from "clsx";
import * as d3 from "d3";
import { useAtomValue } from "jotai";
import { Tooltip } from "radix-ui";
import * as React from "react";

import { useProfileContext } from "@/hooks/profile";
import { profileMetricAtom, type ProfileMetric } from "@/state/profile-metric";
import type { Profile } from "@/types";

type TreemapNode = {
  name: string;
  value?: number;
  children?: TreemapNode[];
};

function createFuncHierarchy(
  profile: Profile,
  metric: ProfileMetric,
): TreemapNode {
  switch (metric) {
    case "memory_peak":
    case "memory_total": {
      const bins: TreemapNode[] = [
        { name: "B", children: [] },
        { name: "KB", children: [] },
        { name: "MB", children: [] },
        { name: "GB", children: [] },
      ];
      const k = 1024;

      profile.pipelines[0].funcs.forEach((func) => {
        const index = Math.floor(Math.log(func[metric]) / Math.log(k));

        if (index >= 0 && index < bins.length) {
          bins[index]?.children?.push({ name: func.name, value: func[metric] });
        }
      });

      return {
        name: profile.pipelines[0].name,
        children: bins,
      };
    }
    default:
      return {
        name: profile.pipelines[0].name,
        children: profile.pipelines[0].funcs.map((func) => {
          return {
            name: func.name,
            value: func[metric],
          };
        }),
      };
  }
}

function formatBytes(d: number) {
  return d3.format(".2s")(d).replace("k", "K").concat("B");
}

const METRIC_TO_FORMATTER: Record<ProfileMetric, (d: number) => string> = {
  memory_peak: formatBytes,
  memory_total: formatBytes,
  stack_peak: (d) => `${d}`,
  num_allocs: (d) => `${d}`,
};

function Treemap() {
  const profile = useProfileContext();
  const profileMetric = useAtomValue(profileMetricAtom);

  const root = React.useMemo(() => {
    const hierarchy = d3
      .hierarchy<TreemapNode>(createFuncHierarchy(profile, profileMetric))
      .sum((d) => d.value ?? 0)
      .sort((a, b) => (b.value ?? 0) - (a.value ?? 0));

    const treemap = d3
      .treemap<TreemapNode>()
      .tile(d3.treemapSquarify)
      .size([960, 720])
      .padding(3)
      .round(true);

    return treemap(hierarchy);
  }, [profile, profileMetric]);

  return (
    <Tooltip.Provider>
      <svg viewBox="0 0 960 720" width="80%" height="80%">
        {root.leaves().map((leaf, index) => (
          <Tooltip.Root delayDuration={0}>
            <g transform={`translate(${leaf.x0}, ${leaf.y0})`}>
              <Tooltip.Trigger key={leaf.data.name} asChild>
                <rect
                  id={`rect-${index}`}
                  x="0"
                  y="0"
                  width={leaf.x1 - leaf.x0}
                  height={leaf.y1 - leaf.y0}
                  className={clsx("fill-oxide-blue/75 stroke-oxide-blue", {
                    "fill-oxide-red/75! stroke-oxide-red!":
                      leaf.parent?.data.name === "GB",
                    "fill-oxide-yellow/75! stroke-oxide-yellow!":
                      leaf.parent?.data.name === "MB",
                    "fill-oxide-green/75! stroke-oxide-green!":
                      leaf.parent?.data.name === "KB",
                    "fill-oxide-purple/75! stroke-oxide-purple!":
                      leaf.parent?.data.name === "B",
                  })}
                />
              </Tooltip.Trigger>
              {leaf.x1 - leaf.x0 > 25 || leaf.y1 - leaf.y0 > 25 ? (
                <>
                  <clipPath id={`clip-${index}`}>
                    <use
                      href={`#rect-${index}`}
                      xlinkHref={`#rect-${index}`}
                    ></use>
                  </clipPath>
                  <text
                    className={clsx(
                      "font-mono text-xs",
                      profileMetric === "memory_peak" ||
                        profileMetric === "memory_total"
                        ? "fill-ps-secondary"
                        : "fill-ps-text-primary",
                    )}
                    clipPath={`url(#clip-${index})`}
                  >
                    <tspan x="8" y="20">
                      {leaf.data.name}
                    </tspan>
                    <tspan x="8" dy="1.5em" className="font-semibold">
                      {METRIC_TO_FORMATTER[profileMetric](leaf.data.value ?? 0)}
                    </tspan>
                  </text>
                </>
              ) : null}
            </g>
            <Tooltip.Portal>
              <Tooltip.Content
                className="bg-ps-primary text-ps-text-primary rounded-xs px-2 py-1 font-mono text-xs"
                sideOffset={5}
              >
                <p>{leaf.data.name}</p>
                <p className="font-semibold">
                  {METRIC_TO_FORMATTER[profileMetric](leaf.data.value ?? 0)}
                </p>
                <Tooltip.Arrow className="fill-ps-primary" />
              </Tooltip.Content>
            </Tooltip.Portal>
          </Tooltip.Root>
        ))}
      </svg>
    </Tooltip.Provider>
  );
}

export default Treemap;
