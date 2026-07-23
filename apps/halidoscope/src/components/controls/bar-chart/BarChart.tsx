import * as Plot from "@observablehq/plot";
import * as d3 from "d3";
import * as React from "react";

interface BarChartProps {
  data: { x: string; y: number }[];
  domain: string[];
  labels: {
    x: string;
    y: string;
  };
  lut: Record<string, string>;
  highlight?: (x: string) => boolean;
}

function BarChart({ data, domain, labels, lut, highlight }: BarChartProps) {
  const ref = React.useRef<HTMLDivElement>(null);

  React.useEffect(() => {
    if (!ref.current) {
      return;
    }

    const plot = Plot.plot({
      style: {
        fontSize: "12px",
      },
      width: 480,
      marginBottom: 80,
      y: {
        grid: true,
        label: labels.y,
        tickFormat: (value) => d3.format(".2s")(value),
        ticks: 8,
      },
      x: {
        domain,
        label: labels.x,
        labelAnchor: "right",
        labelArrow: "right",
        tickSize: 0,
        tickRotate: -45,
        type: "band",
      },
      marks: [
        Plot.barY(data, {
          x: "x",
          y: "y",
          fill: (d) => lut[d.x] ?? "#000000",
          fillOpacity: (d) => (highlight?.(d.x) ? 1 : 0.25),
        }),
      ],
    });

    ref.current.append(plot);

    return () => {
      plot.remove();
    };
  }, [data, labels, domain, lut, highlight]);

  return data.every((d) => d.y === 0) ? (
    <div className="flex h-full flex-col items-center justify-center gap-2">
      <svg
        xmlns="http://www.w3.org/2000/svg"
        className="text-ps-text-secondary h-12 w-12"
        fill="none"
        viewBox="0 0 24 24"
        strokeWidth="1"
        stroke="currentColor"
      >
        <path
          strokeLinecap="round"
          strokeLinejoin="round"
          d="M9.813 15.904L9 18.75l-.813-2.846a4.5 4.5 0 00-3.09-3.09L2.25 12l2.846-.813a4.5 4.5 0 003.09-3.09L9 4.5l.813 2.846a4.5 4.5 0 003.09 3.09L15.75 12l-2.846.813a4.5 4.5 0 00-3.09 3.09zM18.259 8.715L18 9.75l-.259-1.035a3.375 3.375 0 00-2.455-2.456L14.25 6l1.036-.259a3.375 3.375 0 002.455-2.456L18 2.25l.259 1.035a3.375 3.375 0 002.456 2.456L21.75 6l-1.035.259a3.375 3.375 0 00-2.456 2.456z"
        />
      </svg>
      <span className="text-ps-text-secondary text-sm">No data to display</span>
    </div>
  ) : (
    <div ref={ref} />
  );
}

export default BarChart;
