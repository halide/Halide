import { useAtomValue } from "jotai";
import * as React from "react";

import Overlay from "@/components/trace/canvas/Overlay";
import { useTraceContext } from "@/hooks/trace";
import { funcViewAtom, getFuncView } from "@/state/funcView";
import { hoverAtom } from "@/state/hover";
import { packetAtom } from "@/state/packet";
import { renderAtom } from "@/state/render";
import { probeValue, type ProbeResponse } from "@/utils/api";

/** Shows the value of the Func under the mouse as of the current packet. */
function ValueStatus() {
  const { funcs } = useTraceContext();
  const hover = useAtomValue(hoverAtom);
  const packetIndex = useAtomValue(packetAtom);
  const render = useAtomValue(renderAtom);
  const views = useAtomValue(funcViewAtom);
  const [probe, setProbe] = React.useState<{
    func: string;
    result: ProbeResponse;
  } | null>(null);

  const func = hover ? funcs[hover.func] : undefined;
  const slice = func ? getFuncView(views[func.name], func).slice : undefined;
  const color = render.renderMode === "RGB";

  React.useEffect(() => {
    if (!hover || !slice) {
      return;
    }
    let cancelled = false;
    probeValue({
      func: hover.func,
      globalIndex: packetIndex,
      slice,
      color,
      x: hover.x,
      y: hover.y,
    })
      .then((result) => {
        if (!cancelled) {
          setProbe(result ? { func: hover.func, result } : null);
        }
      })
      .catch((err) => console.error(`Failed to probe ${hover.func}: ${err}`));
    return () => {
      cancelled = true;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [hover, packetIndex, color, JSON.stringify(slice)]);

  if (!hover || probe?.func !== hover.func) {
    return null;
  }
  const coords = probe.result.coords.map((c) => c ?? "c").join(", ");
  const values = probe.result.values.map((v) => v ?? "unwritten");
  const value = values.length === 1 ? values[0] : `(${values.join(", ")})`;

  return (
    <Overlay className="top-2 left-2 font-mono">
      {probe.func}({coords}) = {value}
    </Overlay>
  );
}

export default ValueStatus;
