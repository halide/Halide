import * as d3 from "d3";
import { useAtomValue } from "jotai";
import { Separator } from "radix-ui";
import * as React from "react";

import ControlSection from "@/components/controls/ControlSection";
import BarChart from "@/components/controls/bar-chart/BarChart";
import BarChartParameters from "@/components/controls/bar-chart/BarChartParameters";
import Histogram from "@/components/controls/histogram/Histogram";
import HistogramParameters from "@/components/controls/histogram/HistogramParameters";
import RenderMode from "@/components/controls/render/RenderMode";
import { useTraceContext } from "@/hooks/trace";
import { funcAtom } from "@/state/func";
import { type RenderMode as RM, renderAtom } from "@/state/render";
import { tabularDataAtom } from "@/state/tabularData";
import { threadAtom, NO_THREAD_INFO_SENTINEL_ID } from "@/state/thread";

const RENDER_MODE_TO_LABEL: Record<RM, string> = {
  Grayscale: "Value",
  RGB: "",
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

interface HistogramData {
  type: "Histogram";
  data: { x1: number; x2: number; y: number }[];
  domain: [number, number];
  range: string[];
}

interface BarChartData {
  type: "Bar Chart";
  data: { x: string; y: number }[];
  domain: string[];
  range: string[];
}

interface NoChartData {
  type: "No Chart";
  data: number[];
  domain: [number, number];
  range: string[];
}

type ChartData = HistogramData | BarChartData | NoChartData;

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

      return {
        type: "Histogram",
        data: new Array(buckets).fill(0).map((_, i) => ({
          x1: domain[0] + i * step,
          x2: domain[0] + (i + 1) * step,
          y: histogramData?.[i] ?? 0,
        })),
        domain,
        range,
      };
    },
    [],
  );

  const { type, data, domain, range } = React.useMemo((): ChartData => {
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
      default: {
        return { type: "No Chart", data: [], domain: [-1, -1], range: [] };
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
  ]);

  const renderChart = React.useCallback(() => {
    switch (type) {
      case "Histogram":
        return (
          <>
            <Separator.Root className="bg-ps-border-tertiary h-px" />
            <ControlSection title="stats">
              <HistogramParameters />
              <Histogram
                data={data}
                domain={domain}
                range={range}
                scale={scale}
                labels={{
                  x: RENDER_MODE_TO_LABEL[render.renderMode],
                  y: "Coordinate Count",
                }}
              />
            </ControlSection>
            <Separator.Root className="bg-ps-border-tertiary h-px" />
          </>
        );
      case "Bar Chart":
        return (
          <>
            <Separator.Root className="bg-ps-border-tertiary h-px" />
            <ControlSection title="Stats">
              <BarChartParameters />
              <BarChart
                data={data}
                domain={domain}
                range={range}
                labels={{
                  x: RENDER_MODE_TO_LABEL[render.renderMode],
                  y: `${thread.op} Count`,
                }}
                highlight={(x: string) =>
                  x === thread.id || thread.id === NO_THREAD_INFO_SENTINEL_ID
                }
              />
            </ControlSection>
            <Separator.Root className="bg-ps-border-tertiary h-px" />
          </>
        );
      case "No Chart":
        return <Separator.Root className="bg-ps-border-tertiary h-px" />;
    }
  }, [type, data, domain, range, scale, render.renderMode, thread]);

  return (
    <div className="flex flex-col gap-4 px-3 py-4">
      <ControlSection title="Render Mode">
        <RenderMode />
      </ControlSection>
      {renderChart()}
    </div>
  );
}

export default VisualizationPanel;
