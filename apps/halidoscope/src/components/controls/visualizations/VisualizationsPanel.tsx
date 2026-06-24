import { useAtomValue } from "jotai";
import * as React from "react";
import { Label, Separator } from "radix-ui";

import GraphDisplay from "@/components/controls/visualizations/GraphDisplay";
import Histogram from "@/components/controls/visualizations/Histogram";
import HistogramSelect from "@/components/controls/visualizations/HistogramSelect";
import PlaybackRate from "@/components/controls/visualizations/PlaybackRate";
import VisualizationSelect from "@/components/controls/visualizations/VisualizationsSelect";
import { useTraceContext } from "@/hooks/trace";
import { funcAtom } from "@/state/func";
import { histogramAtom, type HistogramScale } from "@/state/histogram";
import {
  type VisualizationMode,
  visualizationModeAtom,
} from "@/state/visualization";
import { FuncMeta } from "@/types";

const VISUALIZATION_MODE_TO_HISTOGRAM_DATA_KEY: Record<
  VisualizationMode,
  keyof FuncMeta | ""
> = {
  "True Values": "",
  "Store Frequency": "store_count_histogram",
  "Load Frequency": "load_count_histogram",
  "Redundant Stores": "redundant_count_histogram",
  "Reuse Distance": "reuse_distance_histogram",
};

const VISUALIZATION_MODE_TO_LABEL: Record<VisualizationMode, string> = {
  "True Values": "",
  "Store Frequency": "Store Count",
  "Load Frequency": "Load Count",
  "Redundant Stores": "Redundant Store Count",
  "Reuse Distance": "Reuse Distance (Packets)",
};

function VisualizationsPanel() {
  const {
    funcs,
    globalMaxStoreCount,
    globalMaxLoadCount,
    globalMaxRedundantCount,
    globalMaxReuseDistance,
  } = useTraceContext();
  const visualizationMode = useAtomValue(visualizationModeAtom);
  const activeFunc = useAtomValue(funcAtom);
  const histogramScale = useAtomValue(histogramAtom) as HistogramScale;

  const dataKey = VISUALIZATION_MODE_TO_HISTOGRAM_DATA_KEY[visualizationMode];
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

    switch (visualizationMode) {
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
    visualizationMode,
    domainMin,
    globalMaxStoreCount,
    globalMaxLoadCount,
    globalMaxRedundantCount,
    globalMaxReuseDistance,
  ]);

  return (
    <div className="flex flex-col gap-4 px-3 py-4">
      <div className="flex flex-col gap-2">
        <Label.Root
          className="text-ps-text-primary font-semibold tracking-widest uppercase"
          htmlFor="visualization-select"
        >
          Visualization
        </Label.Root>
        <VisualizationSelect />
      </div>
      {hasHistogram ? (
        <>
          <Separator.Root className="bg-ps-border-tertiary h-px" />
          <div className="flex flex-col gap-2">
            <Label.Root className="text-ps-text-primary font-semibold tracking-widest uppercase">
              Histogram
            </Label.Root>
            <div className="flex flex-col gap-4">
              <HistogramSelect />
              <Histogram
                data={histogramData}
                domain={histogramDomain}
                labels={{ x: VISUALIZATION_MODE_TO_LABEL[visualizationMode] }}
              />
            </div>
          </div>
        </>
      ) : null}
      <Separator.Root className="bg-ps-border-tertiary h-px" />
      <div className="flex flex-col gap-2">
        <Label.Root className="text-ps-text-primary font-semibold tracking-widest uppercase">
          Parameters
        </Label.Root>
        <PlaybackRate />
        <GraphDisplay />
      </div>
    </div>
  );
}

export default VisualizationsPanel;
