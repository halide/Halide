import { useAtomValue } from "jotai";
import { Separator } from "radix-ui";
import * as React from "react";

import ControlSection from "@/components/controls/ControlSection";
import BarChart from "@/components/controls/bar-chart/BarChart";
import BarChartParameters from "@/components/controls/bar-chart/BarChartParameters";
import GraphDisplay from "@/components/controls/graph/GraphDisplay";
import Histogram from "@/components/controls/histogram/Histogram";
import HistogramParameters from "@/components/controls/histogram/HistogramParameters";
import LivenessControls from "@/components/controls/liveness/LivenessControls";
import PlaybackRate from "@/components/controls/playback/PlaybackRate";
import RenderMode from "@/components/controls/render/RenderMode";
import { useTraceContext } from "@/hooks/trace";
import { funcAtom } from "@/state/func";
import { type RenderMode as RM, renderAtom } from "@/state/render";
import { tabularDataAtom } from "@/state/tabularData";
import { threadAtom } from "@/state/thread";

const RENDER_MODE_TO_LABEL: Record<RM, string> = {
  Grayscale: "",
  RGB: "",
  "Store Frequency": "Store Count",
  "Load Frequency": "Load Count",
  "Redundant Stores": "Redundant Store Count",
  "Reuse Distance": "Reuse Distance (Packets)",
  "Thread Coverage": "Thread ID",
};

interface HistogramData {
  type: "Histogram";
  data: { x1: number; x2: number; y: number }[];
  domain: [number, number];
}

interface BarChartData {
  type: "Bar Chart";
  data: { x: string; y: number }[];
  domain: string[];
}

interface NoChartData {
  type: "No Chart";
  data: number[];
  domain: [number, number];
}

type ChartData = HistogramData | BarChartData | NoChartData;

function VisualizationPanel() {
  const { funcs, stats } = useTraceContext();
  const render = useAtomValue(renderAtom);
  const activeFunc = useAtomValue(funcAtom);
  const { tabularData, scale } = useAtomValue(tabularDataAtom);
  const thread = useAtomValue(threadAtom);
  const min = scale === "log" ? 1 : 0;

  const createHistogramData = React.useCallback(
    (histogramData: Uint32Array, max: number): HistogramData => {
      const buckets = histogramData.length;

      return {
        type: "Histogram",
        data: new Array(buckets).fill(0).map((_, i) => ({
          x1: max > 64 ? Math.round((i / 64) * max) : i,
          x2: max > 64 ? Math.round(((i + 1) / 64) * max) : i + 1,
          y: histogramData?.[i] ?? 0,
        })),
        domain: [min, max + 1],
      };
    },
    [min],
  );

  const { type, data, domain } = React.useMemo((): ChartData => {
    switch (render.renderMode) {
      case "Store Frequency": {
        const max =
          render.normalizationMode === "Per Func"
            ? funcs[activeFunc].max_store_count
            : stats.global_max_store_count;

        return createHistogramData(tabularData ?? new Uint32Array(), max);
      }
      case "Load Frequency": {
        const max =
          render.normalizationMode === "Per Func"
            ? funcs[activeFunc].max_load_count
            : stats.global_max_load_count;

        return createHistogramData(tabularData ?? new Uint32Array(), max);
      }
      case "Redundant Stores": {
        const max =
          render.normalizationMode === "Per Func"
            ? funcs[activeFunc].max_redundant_store_count
            : stats.global_max_redundant_store_count;

        return createHistogramData(tabularData ?? new Uint32Array(), max);
      }
      case "Reuse Distance": {
        const max =
          render.normalizationMode === "Per Func"
            ? funcs[activeFunc].max_reuse_distance
            : stats.global_max_reuse_distance;

        return createHistogramData(tabularData ?? new Uint32Array(), max);
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
        };
      }
      default: {
        return { type: "No Chart", data: [], domain: [-1, -1] };
      }
    }
  }, [
    render,
    tabularData,
    funcs,
    stats,
    activeFunc,
    thread.op,
    createHistogramData,
  ]);

  const renderChart = React.useCallback(() => {
    switch (type) {
      case "Histogram":
        return (
          <>
            <HistogramParameters />
            <Histogram
              data={data}
              domain={domain}
              labels={{
                x: RENDER_MODE_TO_LABEL[render.renderMode],
                y: "Coordinate Count",
              }}
            />
          </>
        );
      case "Bar Chart":
        return (
          <>
            <BarChartParameters />
            <BarChart
              data={data}
              domain={domain}
              labels={{
                x: RENDER_MODE_TO_LABEL[render.renderMode],
                y: `${thread.op} Count`,
              }}
            />
          </>
        );
      case "No Chart":
        return null;
    }
  }, [type, data, domain, render.renderMode, thread.op]);

  return (
    <div className="flex flex-col gap-4 px-3 py-4">
      <ControlSection title="Render Mode">
        <RenderMode />
      </ControlSection>
      {renderChart()}
      <Separator.Root className="bg-ps-border-tertiary h-px" />
      <ControlSection title="Liveness">
        <LivenessControls />
      </ControlSection>
      <Separator.Root className="bg-ps-border-tertiary h-px" />
      <ControlSection title="Graph Display">
        <GraphDisplay />
      </ControlSection>
      <Separator.Root className="bg-ps-border-tertiary h-px" />
      <ControlSection title="Playback">
        <PlaybackRate />
      </ControlSection>
    </div>
  );
}

export default VisualizationPanel;
