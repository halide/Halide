import * as Plot from "@observablehq/plot";
import * as React from "react";

interface BubbleChartProps {
  data: { x: number; y: number; r: number; fill: string; text: string }[];
  dimensions: { width: number; height: number };
  labels: {
    x: string;
    y: string;
    r: string;
  };
  scale: Plot.ScaleType;
  renderLegendLabel?: (value: number) => string;
}

function BubbleChart({
  data,
  dimensions,
  labels,
  scale,
  renderLegendLabel = (value) => String(Math.round(value)),
}: BubbleChartProps) {
  const ref = React.useRef<HTMLDivElement>(null);
  const [scaleR, setScaleR] = React.useState<Plot.Scale>();

  React.useEffect(() => {
    if (!ref.current) {
      return;
    }

    const plot = Plot.plot({
      className: "scatterplot",
      style: {
        fontSize: "12px",
        overflow: "visible",
      },
      width: dimensions.width,
      height: dimensions.height,
      marginBottom: 50,
      y: {
        label: labels.y,
        ticks: 8,
        type: scale,
        grid: true,
      },
      x: {
        label: labels.x,
        labelAnchor: "right",
        labelArrow: "right",
        tickSize: 0,
        tickRotate: -45,
        type: scale,
      },
      r: {
        range: [0, 20],
      },
      marks: [
        Plot.circle(data, {
          x: "x",
          y: "y",
          r: "r",
          fill: "fill",
          fillOpacity: 0.75,
          stroke: "fill",
          strokeWidth: 0.5,
        }),
        Plot.ruleY([1]),
        Plot.tip(
          data,
          Plot.pointer({
            x: "x",
            y: "y",
            title: "text",
            fontFamily: "ui-monospace, SFMono-Regular, Menlo, monospace",
            fontSize: 12,
            textPadding: 8,
          }),
        ),
      ],
    });

    setScaleR(plot.scale("r"));

    ref.current.append(plot);

    return () => {
      plot.remove();
    };
  }, [data, dimensions, labels, scale]);

  const legend = React.useMemo(() => {
    if (!scaleR) {
      return null;
    }

    const radius = (value: number) => scaleR.apply(value) as number;

    // Three representative values whose sqrt-scaled radii halve at each step,
    // rendered smallest-to-largest left to right.
    const max = data.reduce((acc, d) => Math.max(acc, d.r), 0);
    const values = [max / 16, max / 4, max].filter((v) => v > 0);
    if (values.length === 0) {
      return null;
    }

    const titleHeight = 18;
    const labelHeight = 16;
    const maxRadius = radius(values[values.length - 1]);
    const colWidth = 2 * maxRadius;
    const baseline = titleHeight + 2 * maxRadius;
    const height = baseline + labelHeight + 2;
    // Right-align the columns against the SVG's right edge.
    const offsetX = dimensions.width - values.length * colWidth;

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
          x={dimensions.width}
          y="8"
          textAnchor="end"
          className="fill-ps-text-secondary text-xs"
        >
          {labels.r}
        </text>
        {values.map((value, index) => {
          const r = radius(value);
          const cx = offsetX + index * colWidth + colWidth / 2;
          const cy = baseline - r;

          return (
            <g key={index}>
              <circle
                cx={cx}
                cy={cy}
                r={r}
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
  }, [scaleR, data, dimensions.width, labels.r, renderLegendLabel]);

  return (
    <div className="flex flex-col items-end">
      {legend}
      <div ref={ref} className="text-ps-text-secondary" />
    </div>
  );
}

export default BubbleChart;
