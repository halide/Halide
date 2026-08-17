import * as Plot from "@observablehq/plot";
import * as d3 from "d3";
import * as React from "react";

import type { Scale } from "@/state/tabularData";

interface HistogramProps {
  data: { x1: number; x2: number; y0: number; y1: number; color: string }[];
  domain: [number, number];
  scale: Scale;
  labels: {
    x: string;
    y: string;
  };
  renderLegend: boolean;
  interval?: number;
}

function Histogram({
  data,
  domain,
  scale,
  labels,
  renderLegend,
  interval,
}: HistogramProps) {
  const ref = React.useRef<HTMLDivElement>(null);

  React.useEffect(() => {
    if (!ref.current || data.length === 0) {
      return;
    }

    const plot = Plot.plot({
      style: {
        fontSize: "12px",
      },
      width: 480,
      marginBottom: 60,
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
        tickFormat: (value) => d3.format(".2s")(value),
        tickPadding: 24,
        tickSize: 0,
        type: scale,
        interval,
      },
      marks: [
        Plot.rect(data, {
          x1: "x1",
          x2: "x2",
          y1: "y0",
          y2: "y1",
          fill: "color",
        }),
        ...(renderLegend
          ? [
              Plot.ruleY(data, {
                stroke: "color",
                strokeWidth: 8,
                x1: "x1",
                x2: "x2",
                y: 0,
                dy: 12,
              }),
            ]
          : []),
      ],
    });

    ref.current.append(plot);

    return () => {
      plot.remove();
    };
  }, [data, domain, labels, scale, renderLegend, interval]);

  return data.every((d) => d.y1 === d.y0) ? (
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

export default Histogram;
