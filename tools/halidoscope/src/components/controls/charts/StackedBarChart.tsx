import { clsx, type ClassValue } from "clsx";
import * as d3 from "d3";
import { Tooltip } from "radix-ui";
import * as React from "react";

export type BarDatum = {
  name: string;
  value: number;
};

interface Props<T extends BarDatum> {
  barClassname?: (datum: T) => ClassValue;
  data: T[];
  dimensions: {
    width: number;
    height: number;
  };
  renderLabel: (datum: T) => React.ReactNode;
}

const PADDING = 16;
const BAR_HEIGHT = 64;
const LABEL_THRESHOLD = 80;

function StackedBarChart<T extends BarDatum>({
  dimensions,
  data,
  renderLabel,
  barClassname,
}: Props<T>) {
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
                    "fill-oxide-blue/75 stroke-oxide-blue",
                    barClassname?.(datum),
                  )}
                />
              </Tooltip.Trigger>
              <Tooltip.Portal>
                <Tooltip.Content
                  className="bg-ps-primary text-ps-text-primary rounded-xs px-2 py-1 text-xs"
                  sideOffset={5}
                >
                  <div className="flex flex-col">{renderLabel(datum)}</div>
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
                  {renderLabel(datum)}
                </div>
              </foreignObject>
            ) : null,
          )}
        </g>
      </svg>
    </Tooltip.Provider>
  );
}

export default StackedBarChart;
