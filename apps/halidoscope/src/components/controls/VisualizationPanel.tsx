import * as d3 from "d3";
import { useAtomValue } from "jotai";
import { Separator } from "radix-ui";
import * as React from "react";

import ControlSection from "@/components/controls/ControlSection";
import BarChart from "@/components/controls/charts/BarChart";
import Histogram from "@/components/controls/charts/Histogram";
import RenderMode from "@/components/controls/render/RenderMode";
import RenderModeParameters from "@/components/controls/render/RenderModeParameters";
import { useTraceContext } from "@/hooks/trace";
import { funcAtom } from "@/state/func";
import { type RenderMode as RM, renderAtom } from "@/state/render";
import { tabularDataAtom } from "@/state/tabularData";
import { threadAtom, NO_THREAD_INFO_SENTINEL_ID } from "@/state/thread";

const RENDER_MODE_TO_LABEL: Record<RM, string> = {
  Grayscale: "Value",
  RGB: "Value",
  "Store Frequency": "Store Count",
  "Load Frequency": "Load Count",
  "Redundant Stores": "Redundant Store Count",
  "Reuse Distance": "Reuse Distance (Packets)",
  "Thread Coverage": "Thread ID",
};

const METRIC_PALETTE = [
  "#0078D1",
  "#1695F3",
  "#3DACFF",
  "#70C2FF",
  "#D6EEFF",
  "#FFE2D6",
  "#FFBFA3",
  "#FF773D",
  "#FA6400",
  "#D64000",
];

const PURE_CHANNEL_COLORS: Record<"r" | "g" | "b", string> = {
  r: "#ff0000",
  g: "#00ff00",
  b: "#0000ff",
};

const PAIR_CHANNEL_COLORS: Record<string, string> = {
  bg: "#00ffff",
  br: "#ff00ff",
  gr: "#ffff00",
};

const TRIPLE_CHANNEL_COLOR = "#ffffff";

interface HistogramData {
  type: "Histogram";
  data: { x1: number; x2: number; y0: number; y1: number; color: string }[];
  domain: [number, number];
  renderLegend: boolean;
}

interface BarChartData {
  type: "Bar Chart";
  data: { x: string; y: number }[];
  domain: string[];
  range: string[];
}

type ChartData = HistogramData | BarChartData;

function VisualizationPanel() {
  const { funcs, stats } = useTraceContext();
  const render = useAtomValue(renderAtom);
  const activeFunc = useAtomValue(funcAtom);
  const { tabularData, scale } = useAtomValue(tabularDataAtom);
  const thread = useAtomValue(threadAtom);

  const createHistogramData = React.useCallback(
    (
      histogramData: Uint32Array,
      domain: [number, number],
      range: string[],
    ): HistogramData => {
      const buckets = histogramData.length;
      const extent = domain[1] - domain[0];
      const step =
        domain.every(Number.isInteger) && extent <= 64 ? 1 : extent / buckets;
      const colorScale = d3
        .scaleLinear<string>()
        .domain(
          range.map(
            (_, i) => domain[0] + (i * extent) / (range.length - 1 || 1),
          ),
        )
        .range(range)
        .interpolate(d3.interpolateRgb);

      return {
        type: "Histogram",
        data: new Array(buckets).fill(0).map((_, i) => {
          const x1 = domain[0] + i * step;

          return {
            x1,
            x2: domain[0] + (i + 1) * step,
            y0: 0,
            y1: histogramData?.[i] ?? 0,
            color: colorScale(x1),
          };
        }),
        domain: [domain[0], domain[1] + step],
        renderLegend: true,
      };
    },
    [],
  );

  const createRgbHistogramData = React.useCallback(
    (
      channelCounts: [Uint32Array, Uint32Array, Uint32Array],
      domain: [number, number],
    ): HistogramData => {
      const [rCounts, gCounts, bCounts] = channelCounts;
      const buckets = rCounts.length;
      const extent = domain[1] - domain[0];
      const step =
        domain.every(Number.isInteger) && extent <= 64 ? 1 : extent / buckets;

      const data: HistogramData["data"] = [];

      for (let i = 0; i < buckets; i++) {
        const x1 = domain[0] + i * step;
        const x2 = domain[0] + (i + 1) * step;
        const entries = (
          [
            ["r", rCounts[i] ?? 0],
            ["g", gCounts[i] ?? 0],
            ["b", bCounts[i] ?? 0],
          ] as const
        )
          .slice()
          .sort((a, b) => a[1] - b[1]);
        const [lo, mid, hi] = entries;

        // Stack up to three bands per bucket, from the ground up: the height all three
        // channels share (white), then the height the top two share (their pairwise
        // combination), then the remainder of the tallest channel alone (its pure color).
        if (lo[1] > 0) {
          data.push({ x1, x2, y0: 0, y1: lo[1], color: TRIPLE_CHANNEL_COLOR });
        }

        if (mid[1] > lo[1]) {
          const pairKey = [mid[0], hi[0]].sort().join("");
          data.push({
            x1,
            x2,
            y0: lo[1],
            y1: mid[1],
            color: PAIR_CHANNEL_COLORS[pairKey],
          });
        }

        if (hi[1] > mid[1]) {
          data.push({
            x1,
            x2,
            y0: mid[1],
            y1: hi[1],
            color: PURE_CHANNEL_COLORS[hi[0]],
          });
        }
      }

      return {
        type: "Histogram",
        data,
        domain: [domain[0], domain[1] + step],
        renderLegend: false,
      };
    },
    [],
  );

  const chartData = React.useMemo((): ChartData => {
    switch (render.renderMode) {
      case "Grayscale": {
        const min = funcs[activeFunc].min_value ?? 0;
        const max = funcs[activeFunc].max_value ?? 255;

        return createHistogramData(
          tabularData ?? new Uint32Array(),
          [min, max],
          ["#000000", "#ffffff"],
        );
      }
      case "RGB": {
        const min = funcs[activeFunc].min_value ?? 0;
        const max = funcs[activeFunc].max_value ?? 255;
        const domain: [number, number] = [min, max];
        const bins = 256;
        const numChannels = tabularData
          ? Math.floor(tabularData.length / bins)
          : 0;

        if (numChannels >= 3) {
          return createRgbHistogramData(
            [
              tabularData!.slice(0, bins),
              tabularData!.slice(bins, bins * 2),
              tabularData!.slice(bins * 2, bins * 3),
            ],
            domain,
          );
        }

        return createHistogramData(tabularData ?? new Uint32Array(), domain, [
          "#000000",
          "#ffffff",
        ]);
      }
      case "Store Frequency": {
        const min = scale === "log" ? 1 : 0;
        const max =
          render.normalizationMode === "Per Func"
            ? funcs[activeFunc].max_store_count
            : stats.global_max_store_count;

        return createHistogramData(
          tabularData ?? new Uint32Array(),
          [min, max],
          METRIC_PALETTE,
        );
      }
      case "Load Frequency": {
        const min = scale === "log" ? 1 : 0;
        const max =
          render.normalizationMode === "Per Func"
            ? funcs[activeFunc].max_load_count
            : stats.global_max_load_count;

        return createHistogramData(
          tabularData ?? new Uint32Array(),
          [min, max],
          METRIC_PALETTE,
        );
      }
      case "Redundant Stores": {
        const min = scale === "log" ? 1 : 0;
        const max =
          render.normalizationMode === "Per Func"
            ? funcs[activeFunc].max_redundant_store_count
            : stats.global_max_redundant_store_count;

        return createHistogramData(
          tabularData ?? new Uint32Array(),
          [min, max],
          METRIC_PALETTE,
        );
      }
      case "Reuse Distance": {
        const min = scale === "log" ? 1 : 0;
        const max =
          render.normalizationMode === "Per Func"
            ? funcs[activeFunc].max_reuse_distance
            : stats.global_max_reuse_distance;

        return createHistogramData(
          tabularData ?? new Uint32Array(),
          [min, max],
          METRIC_PALETTE,
        );
      }
      case "Thread Coverage": {
        const threadIds = funcs[activeFunc].thread_ids;
        const storeCounts = tabularData?.slice(0, threadIds.length) ?? [];
        const loadCounts =
          tabularData?.slice(threadIds.length, threadIds.length * 2) ?? [];

        return {
          type: "Bar Chart",
          data: threadIds.map((threadId, i) => ({
            x: `${threadId}`,
            y: thread.op === "Store" ? storeCounts[i] : loadCounts[i],
          })),
          domain: threadIds.map((tId) => `${tId}`),
          range: stats.global_thread_ids.reduce<string[]>((acc, el, i) => {
            if (threadIds.includes(el)) {
              return acc.concat(d3.schemeSet3[i]);
            }

            return acc;
          }, []),
        };
      }
    }
  }, [
    render,
    tabularData,
    funcs,
    stats,
    activeFunc,
    scale,
    thread.op,
    createHistogramData,
    createRgbHistogramData,
  ]);

  const renderChart = React.useCallback(() => {
    switch (chartData.type) {
      case "Histogram":
        return (
          <Histogram
            data={chartData.data}
            domain={chartData.domain}
            scale={
              render.renderMode === "Grayscale" || render.renderMode === "RGB"
                ? "linear"
                : scale
            }
            labels={{
              x: RENDER_MODE_TO_LABEL[render.renderMode],
              y: "Coordinate Count",
            }}
            renderLegend={chartData.renderLegend}
            interval={
              render.renderMode !== "Grayscale" && render.renderMode !== "RGB"
                ? 1
                : undefined
            }
          />
        );
      case "Bar Chart":
        return (
          <BarChart
            data={chartData.data}
            domain={chartData.domain}
            range={chartData.range}
            labels={{
              x: RENDER_MODE_TO_LABEL[render.renderMode],
              y: `${thread.op} Count`,
            }}
            highlight={(x: string) =>
              x === thread.id || thread.id === NO_THREAD_INFO_SENTINEL_ID
            }
          />
        );
    }
  }, [chartData, scale, render.renderMode, thread]);

  return (
    <div className="flex flex-col gap-4 px-3 py-4">
      <ControlSection title="Render Mode">
        <RenderMode />
      </ControlSection>
      <Separator.Root className="bg-ps-border-tertiary h-px" />
      <ControlSection title="Stats">
        <RenderModeParameters />
        {renderChart()}
      </ControlSection>
      <Separator.Root className="bg-ps-border-tertiary h-px" />
    </div>
  );
}

export default VisualizationPanel;
