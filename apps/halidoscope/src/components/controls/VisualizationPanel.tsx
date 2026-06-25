import { useAtomValue } from "jotai";
import { Separator } from "radix-ui";
import * as React from "react";

import ControlSection from "@/components/controls/ControlSection";
import GraphDisplay from "@/components/controls/graph/GraphDisplay";
import LivenessControls from "@/components/controls/liveness/LivenessControls";
import PlaybackRate from "@/components/controls/playback/PlaybackRate";
import RenderMode from "@/components/controls/render/RenderMode";
import Histogram from "@/components/controls/histogram/Histogram";
import HistogramSelect from "@/components/controls/histogram/HistogramSelect";
import { useTraceContext } from "@/hooks/trace";
import { funcAtom } from "@/state/func";
import { histogramAtom, type HistogramScale } from "@/state/histogram";
import { type RenderMode as RM, renderModeAtom } from "@/state/render";
import { FuncMeta } from "@/types";

const RENDER_MODE_TO_HISTOGRAM_DATA_KEY: Record<RM, keyof FuncMeta | ""> = {
  Grayscale: "",
  RGB: "",
  "Store Frequency": "store_count_histogram",
  "Load Frequency": "load_count_histogram",
  "Redundant Stores": "redundant_count_histogram",
  "Reuse Distance": "reuse_distance_histogram",
};

const RENDER_MODE_TO_LABEL: Record<RM, string> = {
  Grayscale: "",
  RGB: "",
  "Store Frequency": "Store Count",
  "Load Frequency": "Load Count",
  "Redundant Stores": "Redundant Store Count",
  "Reuse Distance": "Reuse Distance (Packets)",
};

function VisualizationPanel() {
  const {
    funcs,
    globalMaxStoreCount,
    globalMaxLoadCount,
    globalMaxRedundantCount,
    globalMaxReuseDistance,
  } = useTraceContext();
  const renderMode = useAtomValue(renderModeAtom);
  const activeFunc = useAtomValue(funcAtom);
  const histogramScale = useAtomValue(histogramAtom) as HistogramScale;

  const dataKey = RENDER_MODE_TO_HISTOGRAM_DATA_KEY[renderMode];
  const hasHistogram = dataKey && activeFunc && funcs[activeFunc];
  const domainMin = histogramScale === "log" ? 1 : 0;

  const { data: histogramData, domain: histogramDomain } = React.useMemo((): {
    data: { x1: number; x2: number; y: number }[];
    domain: [number, number];
  } => {
    if (!hasHistogram) {
      return { data: [], domain: [domainMin, 1] };
    }

    const data = funcs[activeFunc][dataKey as keyof FuncMeta] as number[];

    switch (renderMode) {
      case "Store Frequency":
        return {
          data: data.map((pixels, i) => ({ x1: i, x2: i + 1, y: pixels })),
          domain: [domainMin, globalMaxStoreCount + 1],
        };
      case "Load Frequency":
        return {
          data: data.map((pixels, i) => ({ x1: i, x2: i + 1, y: pixels })),
          domain: [domainMin, globalMaxLoadCount + 1],
        };
      case "Redundant Stores":
        return {
          data: data.map((pixels, i) => ({ x1: i, x2: i + 1, y: pixels })),
          domain: [domainMin, globalMaxRedundantCount + 1],
        };
      // For Reuse Distance, scale x values to the global max reuse distance
      // since the histogram is normalized to 64 bins.
      case "Reuse Distance":
        return {
          data: data.map((pixels, i) => ({
            x1: Math.round((i / 64) * globalMaxReuseDistance),
            x2: Math.round(((i + 1) / 64) * globalMaxReuseDistance),
            y: pixels,
          })),
          domain: [domainMin, globalMaxReuseDistance],
        };
      default:
        return { data: [], domain: [domainMin, 1] };
    }
  }, [
    hasHistogram,
    activeFunc,
    funcs,
    dataKey,
    renderMode,
    domainMin,
    globalMaxStoreCount,
    globalMaxLoadCount,
    globalMaxRedundantCount,
    globalMaxReuseDistance,
  ]);

  return (
    <div className="flex flex-col gap-4 px-3 py-4">
      <ControlSection title="Render Mode">
        <RenderMode />
      </ControlSection>
      {hasHistogram ? (
        <>
          <Separator.Root className="bg-ps-border-tertiary h-px" />
          <ControlSection title="Histogram">
            <div className="flex flex-col gap-4">
              <HistogramSelect />
              <Histogram
                data={histogramData}
                domain={histogramDomain}
                labels={{ x: RENDER_MODE_TO_LABEL[renderMode] }}
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
