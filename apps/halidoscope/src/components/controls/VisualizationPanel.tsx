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
import { histogramAtom } from "@/state/histogram";
import { type RenderMode as RM, renderAtom } from "@/state/render";
import { threadAtom } from "@/state/thread";

const HISTOGRAM_RENDER_MODES = new Set<RM>([
  "Store Frequency",
  "Load Frequency",
  "Redundant Stores",
  "Reuse Distance",
]);

const BAR_CHART_RENDER_MODES = new Set<RM>(["Thread Coverage"]);

const RENDER_MODE_TO_LABEL: Record<RM, string> = {
  Grayscale: "",
  RGB: "",
  "Store Frequency": "Store Count",
  "Load Frequency": "Load Count",
  "Redundant Stores": "Redundant Store Count",
  "Reuse Distance": "Reuse Distance (Packets)",
  "Thread Coverage": "Thread ID",
};

function VisualizationPanel() {
  const { funcs, globalMaxReuseDistance } = useTraceContext();
  const render = useAtomValue(renderAtom);
  const activeFunc = useAtomValue(funcAtom);
  const { data, scale } = useAtomValue(histogramAtom);
  const thread = useAtomValue(threadAtom);

  const hasHistogram =
    HISTOGRAM_RENDER_MODES.has(render.renderMode) &&
    activeFunc &&
    funcs[activeFunc] &&
    data !== null;
  const domainMin = scale === "log" ? 1 : 0;

  const { data: histogramData, domain: histogramDomain } = React.useMemo((): {
    data: { x1: number; x2: number; y: number }[];
    domain: [number, number];
  } => {
    if (!hasHistogram || !data) {
      return { data: [], domain: [domainMin, 1] };
    }

    switch (render.renderMode) {
      case "Store Frequency":
      case "Load Frequency":
      case "Redundant Stores":
        return {
          data: Array.from(data).map((y, i) => ({
            x1: i,
            x2: i + 1,
            y,
          })),
          domain: [domainMin, data.length],
        };
      case "Reuse Distance": {
        // For Reuse Distance, scale x values based on the normalization mode.
        const domainMax =
          render.normalizationMode === "Per Func"
            ? funcs[activeFunc].max_reuse_distance
            : globalMaxReuseDistance;

        return {
          data: Array.from(data).map((y, i) => ({
            x1: Math.round((i / 64) * domainMax),
            x2: Math.round(((i + 1) / 64) * domainMax),
            y,
          })),
          domain: [domainMin, domainMax],
        };
      }
      default:
        return { data: [], domain: [domainMin, 1] };
    }
  }, [
    hasHistogram,
    data,
    activeFunc,
    funcs,
    render,
    domainMin,
    globalMaxReuseDistance,
  ]);

  const hasBarChart =
    BAR_CHART_RENDER_MODES.has(render.renderMode) &&
    activeFunc &&
    funcs[activeFunc] &&
    data !== null;

  const { data: barChartData } = React.useMemo((): {
    data: { x: string; y: number }[];
  } => {
    if (!hasBarChart || !data) {
      return { data: [] };
    }

    switch (render.renderMode) {
      case "Thread Coverage": {
        const threadIds = funcs[activeFunc].thread_ids;
        const storeCounts = data.slice(0, threadIds.length);
        const loadCounts = data.slice(threadIds.length, threadIds.length * 2);

        return {
          data: threadIds.map((threadId, i) => ({
            x: `${threadId}`,
            y: thread.op === "Store" ? storeCounts[i] : loadCounts[i],
          })),
        };
      }
      default:
        return { data: [] };
    }
  }, [hasBarChart, data, activeFunc, funcs, render, thread.op]);

  return (
    <div className="flex flex-col gap-4 px-3 py-4">
      <ControlSection title="Render Mode">
        <RenderMode />
      </ControlSection>
      {hasHistogram ? (
        <>
          <Separator.Root className="bg-ps-border-tertiary h-px" />
          <ControlSection title="Stats">
            <div className="flex flex-col gap-4">
              <HistogramParameters />
              <Histogram
                data={histogramData}
                domain={histogramDomain}
                labels={{ x: RENDER_MODE_TO_LABEL[render.renderMode] }}
              />
            </div>
          </ControlSection>
        </>
      ) : null}
      {hasBarChart ? (
        <>
          <Separator.Root className="bg-ps-border-tertiary h-px" />
          <ControlSection title="Stats">
            <div className="flex flex-col gap-4">
              <BarChartParameters />
              <BarChart
                data={barChartData}
                labels={{ x: "Thread ID", y: `${thread.op} Count` }}
              />
            </div>
          </ControlSection>
        </>
      ) : null}
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
