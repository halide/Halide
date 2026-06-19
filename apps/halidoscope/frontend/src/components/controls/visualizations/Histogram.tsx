import * as Plot from "@observablehq/plot";
import * as d3 from "d3";
import * as React from "react";

const RAMP_STOPS = 32;
const STOPS = Array.from({ length: RAMP_STOPS + 1 }, (_, i) => {
  const t = i / RAMP_STOPS;
  return { offset: t, color: d3.rgb(d3.interpolateInferno(t)).formatHex() };
});
const RAMP_DY = 12;

interface HistogramProps {
  data: { x: number; y: number }[];
  labels: {
    x: string;
  };
}

function Histogram({ data, labels }: HistogramProps) {
  const ref = React.useRef<HTMLDivElement>(null);
  const gradient = React.useRef<SVGLinearGradientElement>(null);
  const rampId = `histogram-ramp-${React.useId().replace(/:/g, "")}`;

  React.useEffect(() => {
    if (!ref.current) {
      return;
    }

    const plot = Plot.plot({
      style: {
        fontSize: "12px",
      },
      marginBottom: 60,
      y: {
        grid: true,
        label: "Pixel Count",
        tickFormat: (value) => d3.format(".2s")(value),
      },
      x: {
        label: labels.x,
        labelAnchor: "right",
        labelArrow: "right",
        tickSize: 0,
        tickPadding: 24,
      },
      color: {
        scheme: "Inferno",
      },
      marks: [
        Plot.barY(data, { x: "x", y: "y", fill: "x" }),
        Plot.ruleY([0], {
          stroke: `url(#${rampId})`,
          strokeWidth: 8,
          dy: RAMP_DY,
        }),
      ],
    });

    ref.current.append(plot);

    const xScale = plot.scale("x");
    if (xScale && gradient.current && data.length > 0) {
      const bandwidth = xScale.bandwidth ?? 0;
      const left = xScale.apply(data[0]!.x);
      const right = xScale.apply(data[data.length - 1]!.x) + bandwidth;

      gradient.current.setAttribute("x1", String(left));
      gradient.current.setAttribute("x2", String(right));

      // Plot draws the rule across the full frame; clip its rendered line(s) to the same
      // footprint so the strip starts and ends with the bars rather than at the axes. The rule
      // group is the only element stroked with our gradient, so we can find it by that url.
      plot
        .querySelector(`[stroke="url(#${rampId})"]`)
        ?.querySelectorAll("line")
        .forEach((line) => {
          line.setAttribute("x1", String(left));
          line.setAttribute("x2", String(right));
        });
    }

    return () => {
      plot.remove();
    };
  }, [data, labels, rampId]);

  return (
    <>
      <svg
        width="0"
        height="0"
        aria-hidden="true"
        style={{ position: "absolute" }}
      >
        <defs>
          <linearGradient
            ref={gradient}
            id={rampId}
            gradientUnits="userSpaceOnUse"
            x1="0"
            y1="0"
            x2="1"
            y2="0"
          >
            {STOPS.map((stop) => (
              <stop
                key={stop.offset}
                offset={stop.offset}
                stopColor={stop.color}
              />
            ))}
          </linearGradient>
        </defs>
      </svg>
      <div ref={ref} />
    </>
  );
}

export default Histogram;
