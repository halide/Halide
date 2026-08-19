import * as d3 from "d3";
import { Tooltip } from "radix-ui";
import * as React from "react";

interface BubbleChartProps {
  data: { id: string; x: number; y: number; r: number; color: string }[];
  dimensions: { width: number; height: number };
  labels: {
    x: string;
    y: string;
    r: string;
  };
  renderLegendLabel?: (value: number) => string;
  renderTooltip?: ({
    id,
    x,
    y,
    r,
  }: {
    id: string;
    x: number;
    y: number;
    r: number;
  }) => React.ReactNode;
}

function BubbleChart({
  data,
  dimensions,
  labels,
  renderLegendLabel = (value) => String(Math.round(value)),
  renderTooltip = () => null,
}: BubbleChartProps) {
  const margin = {
    top: 25,
    right: 20,
    left: 50,
    bottom: 50,
  };

  const x = d3
    .scaleLog()
    .domain(d3.extent(data, (d) => d.x) as [number, number])
    .nice()
    .range([margin.left, dimensions.width - margin.right]);

  const y = d3
    .scaleLog()
    .domain(d3.extent(data, (d) => d.y) as [number, number])
    .nice()
    .range([dimensions.height - margin.bottom, margin.top]);

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
          .tickSize(-(dimensions.width - margin.left - margin.right)),
      )
      .call((g) => g.select(".domain").remove())
      .call((g) => g.selectAll(".tick line").attr("stroke-opacity", 0.1));
  }, [y, dimensions.width, margin.left, margin.right]);

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
    const offsetX = dimensions.width - margin.right - values.length * colWidth;

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
        <text
          x={dimensions.width - margin.right}
          y="8"
          textAnchor="end"
          className="fill-ps-text-secondary text-xs"
        >
          {labels.r}
        </text>
        {values.map((value, index) => {
          const radius = r(value);
          const cx = offsetX + index * colWidth + colWidth / 2;
          const cy = baseline - radius;

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
                {renderLegendLabel(value)}
              </text>
            </g>
          );
        })}
      </svg>
    );
  }, [data, dimensions.width, labels.r, margin.right, r, renderLegendLabel]);

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
            transform={`translate(0, ${dimensions.height - margin.bottom + 4})`}
            className="text-ps-text-secondary text-xs"
          />
          <g
            ref={gyRef}
            transform={`translate(${margin.left}, 0)`}
            className="text-ps-text-secondary text-xs"
          />
          <text
            x={dimensions.width - margin.right}
            y={dimensions.height - 6}
            textAnchor="end"
            className="fill-ps-text-secondary text-xs"
          >
            {labels.x} →
          </text>
          <text
            x={margin.left}
            y={margin.top - 10}
            textAnchor="start"
            className="fill-ps-text-secondary text-xs"
          >
            ↑ {labels.y}
          </text>
          <g>
            {data.map((d, i) => (
              <Tooltip.Root key={i} delayDuration={0}>
                <Tooltip.Trigger asChild>
                  <circle
                    cx={x(d.x)}
                    cy={y(d.y)}
                    r={r(d.r)}
                    fill={d.color}
                    fillOpacity={0.75}
                    strokeWidth="0.5"
                    stroke={d.color}
                  />
                </Tooltip.Trigger>
                <Tooltip.Portal>
                  <Tooltip.Content
                    className="bg-ps-primary text-ps-text-primary text-tiny rounded-xs px-2 py-1"
                    sideOffset={5}
                  >
                    {renderTooltip({ id: d.id, x: d.x, y: d.y, r: d.r })}
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

export default BubbleChart;
