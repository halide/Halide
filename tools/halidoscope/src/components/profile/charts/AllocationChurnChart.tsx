import { clsx } from "clsx";
import * as d3 from "d3";
import { Tooltip } from "radix-ui";
import * as React from "react";

import { useProfileContext } from "@/hooks/profile";
import { formatBytes } from "@/utils/formatters";

interface ByteStringProps {
  value: number;
  className?: string;
}

function ByteString({ value, className }: ByteStringProps) {
  const { value: byteValue, unit } = formatBytes(value);

  return (
    <p className={clsx("flex font-semibold", className)}>
      <span>{byteValue}</span>
      <span>&thinsp;</span>
      <span
        className={clsx({
          "text-oxide-purple": unit === "B",
          "text-oxide-green": unit === "KB",
          "text-oxide-yellow": unit === "MB",
          "text-oxide-red": unit === "GB",
        })}
      >
        {unit}
      </span>
    </p>
  );
}

interface Props {
  dimensions: { width: number; height: number };
}

const MARGIN = {
  top: 25,
  right: 20,
  left: 50,
  bottom: 50,
};

function AllocationChurnChart({ dimensions }: Props) {
  const { pipelines } = useProfileContext();

  const { funcs, runs } = pipelines[0];

  const data = React.useMemo(() => {
    const k = 1024;
    const bins = ["B", "KB", "MB", "GB"];

    return funcs
      .map((func) => {
        const index = Math.floor(Math.log(func.memory_peak) / Math.log(k));
        const byteMagnitude = bins[index];

        return {
          id: func.name,
          x: func.memory_peak,
          y: func.num_allocs / runs,
          r: func.memory_total / runs,
          byteMagnitude,
        };
      })
      .filter((d) => d.x > 0 && d.y > 0);
  }, [funcs, runs]);

  const x = d3
    .scaleLog()
    .domain(d3.extent(data, (d) => d.x) as [number, number])
    .nice()
    .range([MARGIN.left, dimensions.width - MARGIN.right]);

  const y = d3
    .scaleLog()
    .domain(d3.extent(data, (d) => d.y) as [number, number])
    .nice()
    .range([dimensions.height - MARGIN.bottom, MARGIN.top]);

  const r = d3
    .scaleSqrt()
    .domain(d3.extent(data, (d) => d.r) as [number, number])
    .range([1, 25]);

  const gxRef = React.useRef<SVGGElement>(null);
  const gyRef = React.useRef<SVGGElement>(null);

  // No tick marks, labels rotated -45°: matches the original Plot config
  // (tickSize: 0, tickRotate: -45).
  React.useEffect(() => {
    if (!gxRef.current) {
      return;
    }

    d3.select(gxRef.current)
      .call(d3.axisBottom(x).tickSize(0))
      .call((g) => g.select(".domain").remove())
      .selectAll("text")
      .attr("transform", "rotate(-45)")
      .style("text-anchor", "end");
  }, [x]);

  // Full-width gridlines instead of a left baseline: matches the original
  // Plot config (grid: true, ticks: 8).
  React.useEffect(() => {
    if (!gyRef.current) {
      return;
    }

    d3.select(gyRef.current)
      .call(
        d3
          .axisLeft(y)
          .ticks(8)
          .tickSize(-(dimensions.width - MARGIN.left - MARGIN.right)),
      )
      .call((g) => g.select(".domain").remove())
      .call((g) => g.selectAll(".tick line").attr("stroke-opacity", 0.1));
  }, [y, dimensions.width]);

  const legend = React.useMemo(() => {
    // Three representative values whose sqrt-scaled radii halve at each step,
    // rendered smallest-to-largest left to right.
    const max = data.reduce((acc, d) => Math.max(acc, d.r), 0);
    const values = [max / 16, max / 4, max].filter((v) => v > 0);
    if (values.length === 0) {
      return null;
    }

    const titleHeight = 18;
    const labelHeight = 16;
    const maxRadius = r(values[values.length - 1]);
    const colWidth = 2 * maxRadius;
    const baseline = titleHeight + 2 * maxRadius;
    const height = baseline + labelHeight + 2;
    // Right-align the columns against the SVG's right edge.
    const offsetX = dimensions.width - MARGIN.right - values.length * colWidth;

    return (
      <svg
        viewBox={`0 0 ${dimensions.width} ${height}`}
        width={dimensions.width}
        style={{
          maxWidth: "100%",
          height: "auto",
          fontFamily: "system-ui, sans-serif",
        }}
      >
        <g transform={`translate(${offsetX - 3 * colWidth}, 0)`}>
          <text
            x={MARGIN.left}
            y="8"
            className="fill-ps-text-secondary text-xs"
          >
            Peak Memory
            <tspan dy={labelHeight} x={MARGIN.left} className="text-tiny">
              (Byte Order of Mag.)
            </tspan>
          </text>
          <text
            x={r(values[0]) + MARGIN.left}
            y={baseline + labelHeight}
            textAnchor="middle"
            className="fill-ps-text-secondary text-xs"
          >
            B
          </text>
          <circle
            r={r(values[0])}
            cx={r(values[0]) + MARGIN.left}
            cy={baseline - r(values[0])}
            className="fill-oxide-purple/75 stroke-oxide-purple"
          />
          <text
            x={4 * r(values[0]) + MARGIN.left}
            y={baseline + labelHeight}
            textAnchor="middle"
            className="fill-ps-text-secondary text-xs"
          >
            KB
          </text>
          <circle
            r={r(values[0])}
            cx={4 * r(values[0]) + MARGIN.left}
            cy={baseline - r(values[0])}
            className="fill-oxide-green/75 stroke-oxide-green"
          />
          <text
            x={7 * r(values[0]) + MARGIN.left}
            y={baseline + labelHeight}
            textAnchor="middle"
            className="fill-ps-text-secondary text-xs"
          >
            MB
          </text>
          <circle
            r={r(values[0])}
            cx={7 * r(values[0]) + MARGIN.left}
            cy={baseline - r(values[0])}
            className="fill-oxide-yellow/75 stroke-oxide-yellow"
          />
          <text
            x={10 * r(values[0]) + MARGIN.left}
            y={baseline + labelHeight}
            textAnchor="middle"
            className="fill-ps-text-secondary text-xs"
          >
            GB
          </text>
          <circle
            r={r(values[0])}
            cx={10 * r(values[0]) + MARGIN.left}
            cy={baseline - r(values[0])}
            className="fill-oxide-red/75 stroke-oxide-red"
          />
        </g>
        <text
          x={dimensions.width - MARGIN.right}
          y="8"
          textAnchor="end"
          className="fill-ps-text-secondary text-xs"
        >
          Total Memory
        </text>
        {values.map((value, index) => {
          const radius = r(value);
          const cx = offsetX + index * colWidth + colWidth / 2;
          const cy = baseline - radius;
          const { value: byteValue, unit } = formatBytes(value);

          return (
            <g key={index}>
              <circle
                cx={cx}
                cy={cy}
                r={radius}
                strokeWidth={0.5}
                className="fill-ps-text-secondary/75 stroke-ps-text-secondary"
              />
              <text
                x={cx}
                y={baseline + labelHeight}
                textAnchor="middle"
                className="fill-ps-text-secondary text-xs"
              >
                {`${byteValue}\u2009${unit}`}
              </text>
            </g>
          );
        })}
      </svg>
    );
  }, [data, dimensions.width, r]);

  return (
    <Tooltip.Provider>
      <div className="flex flex-col items-end">
        {legend}
        <svg
          viewBox={`0 0 ${dimensions.width} ${dimensions.height}`}
          width={dimensions.width}
          height={dimensions.height}
          style={{ maxWidth: "100%", height: "auto" }}
        >
          <g
            ref={gxRef}
            transform={`translate(0, ${dimensions.height - MARGIN.bottom + 4})`}
            className="text-ps-text-secondary text-xs"
          />
          <g
            ref={gyRef}
            transform={`translate(${MARGIN.left}, 0)`}
            className="text-ps-text-secondary text-xs"
          />
          <text
            x={dimensions.width - MARGIN.right}
            y={dimensions.height - 6}
            textAnchor="end"
            className="fill-ps-text-secondary text-xs"
          >
            Peak Memory →
          </text>
          <text
            x={MARGIN.left}
            y={MARGIN.top - 10}
            textAnchor="start"
            className="fill-ps-text-secondary text-xs"
          >
            ↑ Num Allocs
          </text>
          <g>
            {data.map((d, i) => (
              <Tooltip.Root key={i} delayDuration={0}>
                <Tooltip.Trigger asChild>
                  <circle
                    cx={x(d.x)}
                    cy={y(d.y)}
                    r={r(d.r)}
                    fillOpacity={0.75}
                    strokeWidth="0.5"
                    className={clsx({
                      "fill-oxide-purple/75 stroke-oxide-purple":
                        d.byteMagnitude === "B",
                      "fill-oxide-green/75 stroke-oxide-green":
                        d.byteMagnitude === "KB",
                      "fill-oxide-yellow/75 stroke-oxide-yellow":
                        d.byteMagnitude === "MB",
                      "fill-oxide-red/75 stroke-oxide-red":
                        d.byteMagnitude === "GB",
                    })}
                  />
                </Tooltip.Trigger>
                <Tooltip.Portal>
                  <Tooltip.Content
                    className="bg-ps-primary text-ps-text-primary text-tiny rounded-xs px-2 py-1"
                    sideOffset={5}
                  >
                    <div className="flex flex-col items-center gap-1.5">
                      <p>{d.id}</p>
                      <div className="grid grid-cols-2 gap-x-0.5 gap-y-px">
                        <p>Peak Memory</p>
                        <ByteString value={d.x} className="justify-self-end" />
                        <p>Total Memory</p>
                        <ByteString value={d.r} className="justify-self-end" />
                        <p>Num Allocs</p>
                        <p className="text-right font-semibold">{d.y}</p>
                      </div>
                    </div>
                    <Tooltip.Arrow className="fill-ps-primary" />
                  </Tooltip.Content>
                </Tooltip.Portal>
              </Tooltip.Root>
            ))}
          </g>
        </svg>
      </div>
    </Tooltip.Provider>
  );
}

export default AllocationChurnChart;
