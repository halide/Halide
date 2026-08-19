import { clsx } from "clsx";
import * as d3 from "d3";
import { Tooltip } from "radix-ui";
import * as React from "react";

import { useProfileContext } from "@/hooks/profile";
import { ProfilePipeline, type ProfileFunc } from "@/types/profile";

interface SegmentLabelProps {
  billed_runs: ProfilePipeline["billed_runs"];
  kind: ProfileFunc["kind"];
  name: ProfileFunc["name"];
  sampledTotal: number;
  value: number;
}

function SegmentLabel({
  billed_runs,
  kind,
  name,
  sampledTotal,
  value,
}: SegmentLabelProps) {
  return (
    <div
      className={clsx("flex flex-col", {
        "text-ps-text-primary": kind !== 0,
      })}
    >
      <span>{name}</span>
      <span>
        <span className="font-semibold">
          {(value * 1e-6).toFixed(2)}&thinsp;ms
        </span>{" "}
        ({((value / billed_runs / sampledTotal) * 100).toFixed(1)}
        %)
      </span>
    </div>
  );
}

interface Props {
  dimensions: {
    width: number;
    height: number;
  };
}

const PADDING = 16;
const BAR_HEIGHT = 64;
const LABEL_THRESHOLD = 80;

function RuntimeChart({ dimensions }: Props) {
  const { pipelines } = useProfileContext();

  const { funcs, billed_runs } = pipelines[0];

  const data = React.useMemo(() => {
    return funcs
      .filter((func) => func.time_ns > 0)
      .map((func) => ({
        name: func.name,
        kind: func.kind,
        value: func.time_ns / billed_runs,
      }));
  }, [billed_runs, funcs]);

  const segments = React.useMemo(() => {
    const sorted = [...data].sort((a, b) => b.value - a.value);
    const total = d3.sum(sorted, (d) => d.value)!;

    const x = d3
      .scaleLinear()
      .domain([0, total])
      .range([0, dimensions.width - 2 * PADDING]);

    // Inclusive prefix sums, so each segment starts where the previous ended.
    const ends = d3.cumsum(sorted, (d) => d.value);

    return sorted.map((datum, index) => {
      const start = x(ends[index] - datum.value);

      return { datum, x: start, width: x(ends[index]) - start };
    });
  }, [data, dimensions.width]);

  const sampledTotal = React.useMemo(
    () => data.reduce((acc, datum) => acc + datum.value / billed_runs, 0),
    [billed_runs, data],
  );

  return (
    <Tooltip.Provider>
      <svg
        viewBox={`0 0 ${dimensions.width} ${dimensions.height}`}
        width="100%"
        height="100%"
      >
        <g
          transform={`translate(${PADDING}, ${(dimensions.height - BAR_HEIGHT) / 2})`}
        >
          {segments.map(({ datum, x, width }) => (
            <Tooltip.Root key={datum.name} delayDuration={0}>
              <Tooltip.Trigger asChild>
                <rect
                  x={x}
                  y={0}
                  width={width}
                  height={BAR_HEIGHT}
                  strokeWidth={1}
                  className={clsx(
                    {
                      "fill-oxide-blue/75 stroke-oxide-blue": datum.kind === 0,
                    },
                    {
                      "fill-ps-text-secondary/50 stroke-ps-text-secondary":
                        datum.kind !== 0,
                    },
                  )}
                />
              </Tooltip.Trigger>
              <Tooltip.Portal>
                <Tooltip.Content
                  className="bg-ps-primary text-ps-text-primary rounded-xs px-2 py-1 text-xs"
                  sideOffset={5}
                >
                  <SegmentLabel
                    billed_runs={billed_runs}
                    kind={datum.kind}
                    name={datum.name}
                    sampledTotal={sampledTotal}
                    value={datum.value}
                  />
                  <Tooltip.Arrow className="fill-ps-primary" />
                </Tooltip.Content>
              </Tooltip.Portal>
            </Tooltip.Root>
          ))}
          {segments.map(({ datum, x, width }) =>
            width > LABEL_THRESHOLD ? (
              <foreignObject
                key={`label-${datum.name}`}
                x={x}
                y={0}
                width={width}
                height={BAR_HEIGHT}
                pointerEvents="none"
              >
                <div className="text-ps-secondary flex h-full w-full flex-col items-center justify-center overflow-hidden px-1 text-center text-xs leading-tight whitespace-nowrap">
                  <SegmentLabel
                    billed_runs={billed_runs}
                    kind={datum.kind}
                    name={datum.name}
                    sampledTotal={sampledTotal}
                    value={datum.value}
                  />
                </div>
              </foreignObject>
            ) : null,
          )}
        </g>
      </svg>
    </Tooltip.Provider>
  );
}

export default RuntimeChart;
