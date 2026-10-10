import { useAtom, useAtomValue } from "jotai";
import * as React from "react";

import Select from "@/components/shared/Select";
import ValueControls from "@/components/trace/controls/ValueControls";
import { useFuncView } from "@/hooks/funcView";
import { useTraceContext } from "@/hooks/trace";
import { funcAtom } from "@/state/func";
import { type NormalizationMode, renderAtom } from "@/state/render";
import { tabularDataAtom, type Scale } from "@/state/tabularData";
import { threadAtom, NO_THREAD_INFO_SENTINEL_ID } from "@/state/thread";

function RenderModeParameters() {
  const { funcs } = useTraceContext();
  const activeFunc = useAtomValue(funcAtom);
  const [render, setRender] = useAtom(renderAtom);
  const [{ renderMode }] = useFuncView(funcs[activeFunc]);
  const [tabularData, setTabularData] = useAtom(tabularDataAtom);
  const [thread, setThread] = useAtom(threadAtom);

  const renderSecondaryControls = React.useCallback(() => {
    switch (renderMode) {
      case "Grayscale":
      case "Plot":
      case "RGB":
        return funcs[activeFunc] ? (
          <ValueControls key={activeFunc} func={funcs[activeFunc]} />
        ) : null;
      case "Store Frequency":
      case "Load Frequency":
      case "Redundant Stores":
      case "Reuse Distance":
        return (
          <div className="grid grid-cols-2 gap-2">
            <Select
              id="scale-select"
              label="Scale"
              value={tabularData.scale}
              onValueChange={(value) =>
                setTabularData({ ...tabularData, scale: value as Scale })
              }
              items={[
                { value: "linear", label: "Linear" },
                { value: "log", label: "Log" },
              ]}
            />
            <Select
              id="normalization-select"
              label="Normalize Display"
              value={render.normalizationMode}
              onValueChange={(value) =>
                setRender({
                  ...render,
                  normalizationMode: value as NormalizationMode,
                })
              }
              items={[
                { value: "Across Funcs", label: "Across Funcs" },
                { value: "Per Func", label: "Per Func" },
              ]}
            />
          </div>
        );
      case "Thread Coverage":
        return (
          <div className="grid grid-cols-2 gap-2">
            <Select
              id="op-select"
              label="Operation"
              value={thread.op}
              onValueChange={(value) => {
                setThread({ ...thread, op: value as "Load" | "Store" });
              }}
              items={[
                { value: "Store", label: "Store" },
                { value: "Load", label: "Load" },
              ]}
            />
            <Select
              id="thread-filter-select"
              label="Filter by Thread"
              value={
                thread.id === NO_THREAD_INFO_SENTINEL_ID ? "None" : thread.id
              }
              onValueChange={(value) => {
                setThread({
                  ...thread,
                  id: value === "None" ? NO_THREAD_INFO_SENTINEL_ID : value,
                });
              }}
              items={[
                { value: "None", label: "None" },
                ...funcs[activeFunc].thread_ids
                  .filter((threadId) => threadId !== "0")
                  .map((threadId) => ({ value: threadId, label: threadId })),
              ]}
            />
          </div>
        );
    }
  }, [
    renderMode,
    render,
    setRender,
    tabularData,
    setTabularData,
    thread,
    setThread,
    funcs,
    activeFunc,
  ]);

  return renderSecondaryControls();
}

export default RenderModeParameters;
