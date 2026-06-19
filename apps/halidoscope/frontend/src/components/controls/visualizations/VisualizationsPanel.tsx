import { useAtomValue } from "jotai";
import { Label, Separator } from "radix-ui";

import FuncSelect from "@/components/controls/visualizations/FuncSelect";
import Histogram from "@/components/controls/visualizations/Histogram";
import PlaybackRate from "@/components/controls/visualizations/PlaybackRate";
import VisualizationSelect from "@/components/controls/visualizations/VisualizationsSelect";
import { useTraceContext } from "@/hooks/trace";
import { funcAtom } from "@/state/func";
import {
  type VisualizationMode,
  visualizationModeAtom,
} from "@/state/visualization";
import { FuncMeta } from "@/types";

const VISUALIZATION_MODE_TO_HISTOGRAM_DATA_KEY: Record<
  VisualizationMode,
  keyof FuncMeta
> = {
  "True Values": "",
  "Store Frequency": "store_count_histogram",
  "Load Frequency": "load_count_histogram",
  "Redundant Stores": "redundant_count_histogram",
};

const VISUALIZATION_MODE_TO_LABEL: Record<VisualizationMode, string> = {
  "True Values": "",
  "Store Frequency": "Store Count",
  "Load Frequency": "Load Count",
  "Redundant Stores": "Redundant Store Count",
};

function VisualizationsPanel() {
  const { funcs } = useTraceContext();
  const visualizationMode = useAtomValue(visualizationModeAtom);
  const func = useAtomValue(funcAtom);

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
      {func && funcs[func] && visualizationMode !== "True Values" ? (
        <>
          <Separator.Root className="bg-ps-border-tertiary h-px" />
          <div className="flex flex-col gap-2">
            <Label.Root className="text-ps-text-primary font-semibold tracking-widest uppercase">
              Histogram
            </Label.Root>
            <>
              <FuncSelect />
              <Histogram
                data={
                  (
                    funcs[func][
                      VISUALIZATION_MODE_TO_HISTOGRAM_DATA_KEY[
                        visualizationMode
                      ]
                    ] as number[]
                  ).map((pixels, stores) => ({
                    x: stores,
                    y: pixels,
                  })) ?? []
                }
                labels={{ x: VISUALIZATION_MODE_TO_LABEL[visualizationMode] }}
              />
            </>
          </div>
        </>
      ) : null}
      <Separator.Root className="bg-ps-border-tertiary h-px" />
      <div className="flex flex-col gap-2">
        <Label.Root className="text-ps-text-primary font-semibold tracking-widest uppercase">
          Parameters
        </Label.Root>
        <PlaybackRate />
      </div>
    </div>
  );
}

export default VisualizationsPanel;
