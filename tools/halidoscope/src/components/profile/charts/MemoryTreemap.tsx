import { clsx } from "clsx";
import * as d3 from "d3";
import { Tooltip } from "radix-ui";
import * as React from "react";

import { useProfileContext } from "@/hooks/profile";
import { formatBytes } from "@/utils/formatters";

export type TreemapNode = {
  name: string;
  value?: number;
  children?: TreemapNode[];
};

interface Props {
  dimensions: {
    width: number;
    height: number;
  };
}

function Treemap({ dimensions }: Props) {
  const { pipelines } = useProfileContext();
  const { funcs, name } = pipelines[0];

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

  const root = React.useMemo(() => {
    const treemapHierarchy = d3
      .hierarchy<TreemapNode>(hierarchy)
      .sum((d) => d.value ?? 0)
      .sort((a, b) => (b.value ?? 0) - (a.value ?? 0));

    const treemap = d3
      .treemap<TreemapNode>()
      .tile(d3.treemapSquarify)
      .size([dimensions.width, dimensions.height])
      .padding(3)
      .round(true);

    return treemap(treemapHierarchy);
  }, [hierarchy, dimensions]);

  return (
    <Tooltip.Provider>
      <svg
        viewBox={`0 0 ${dimensions.width} ${dimensions.height}`}
        width="100%"
        height="100%"
      >
        {root.leaves().map((leaf, index) => {
          const { value, unit } = formatBytes(leaf.data.value ?? 0);

          return (
            <Tooltip.Root delayDuration={0} key={leaf.data.name}>
              <g transform={`translate(${leaf.x0}, ${leaf.y0})`}>
                <Tooltip.Trigger asChild>
                  <rect
                    id={`rect-${index}`}
                    x="0"
                    y="0"
                    width={leaf.x1 - leaf.x0}
                    height={leaf.y1 - leaf.y0}
                    className={clsx({
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
                {leaf.x1 - leaf.x0 > 25 && leaf.y1 - leaf.y0 > 25 ? (
                  <>
                    <clipPath id={`clip-${index}`}>
                      <use
                        href={`#rect-${index}`}
                        xlinkHref={`#rect-${index}`}
                      ></use>
                    </clipPath>
                    <text
                      className="fill-ps-secondary text-tiny"
                      clipPath={`url(#clip-${index})`}
                    >
                      <tspan x="8" y="20">
                        {leaf.data.name}
                      </tspan>
                      <tspan x="8" dy="1.5em" className="font-semibold">
                        {`${value}\u2009${unit}`}
                      </tspan>
                    </text>
                  </>
                ) : null}
              </g>
              <Tooltip.Portal>
                <Tooltip.Content
                  className="bg-ps-primary text-ps-text-primary text-tiny rounded-xs px-2 py-1"
                  sideOffset={5}
                >
                  <p>{leaf.data.name}</p>
                  <p className="font-semibold">{`${value}\u2009${unit}`}</p>
                  <Tooltip.Arrow className="fill-ps-primary" />
                </Tooltip.Content>
              </Tooltip.Portal>
            </Tooltip.Root>
          );
        })}
      </svg>
    </Tooltip.Provider>
  );
}

export default Treemap;
