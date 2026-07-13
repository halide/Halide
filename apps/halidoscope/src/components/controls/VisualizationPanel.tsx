import { useAtomValue } from "jotai";
import { Separator } from "radix-ui";
import * as React from "react";

import ControlSection from "@/components/controls/ControlSection";
import GraphDisplay from "@/components/controls/graph/GraphDisplay";
import LivenessControls from "@/components/controls/liveness/LivenessControls";
import PlaybackRate from "@/components/controls/playback/PlaybackRate";
import RenderMode from "@/components/controls/render/RenderMode";
import Histogram from "@/components/controls/histogram/Histogram";
import HistogramSelect from "@/components/controls/histogram/HistogramParameters";
import { useTraceContext } from "@/hooks/trace";
import { funcAtom } from "@/state/func";
import { histogramAtom } from "@/state/histogram";
import { type RenderMode as RM, renderAtom } from "@/state/render";

const HISTOGRAM_RENDER_MODES = new Set<RM>([
  "Store Frequency",
  "Load Frequency",
  "Redundant Stores",
  "Reuse Distance",
]);

const RENDER_MODE_TO_LABEL: Record<RM, string> = {
  Grayscale: "",
  RGB: "",
  "Store Frequency": "Store Count",
  "Load Frequency": "Load Count",
  "Redundant Stores": "Redundant Store Count",
  "Reuse Distance": "Reuse Distance (Packets)",
};

function VisualizationPanel() {
  const { funcs, globalMaxReuseDistance } = useTraceContext();
  const render = useAtomValue(renderAtom);
  const activeFunc = useAtomValue(funcAtom);
  const { data, scale } = useAtomValue(histogramAtom);

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
                labels={{ x: RENDER_MODE_TO_LABEL[render.renderMode] }}
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
