import {
  getIncomers,
  getOutgoers,
  Handle,
  type Node,
  type NodeProps,
  Position,
  useEdges,
  useNodes,
  useViewport,
} from "@xyflow/react";
import clsx from "clsx";
import { useAtomValue } from "jotai";
import * as React from "react";

import HandleCircle from "@/components/shared/HandleCircle";
import type { FuncMeta } from "@/types";
import { packetAtom } from "@/state/packet";
import { visualizationModeAtom } from "@/state/visualization";
import {
  renderAt,
  renderHeatmap,
  renderRedundant,
  renderReuseDistance,
} from "@/utils/api";

type FuncNode = Node<FuncMeta, "funcCanvas">;

function FuncCanvas({
  data: { name, width, height, liveness_start, liveness_end },
}: NodeProps<FuncNode>) {
  const canvasRef = React.useRef<HTMLCanvasElement>(null);
  const globalIndex = useAtomValue(packetAtom);
  const visualizationMode = useAtomValue(visualizationModeAtom);
  const isFuncBufferLive = React.useMemo(
    () => liveness_start <= globalIndex && globalIndex <= liveness_end,
    [liveness_start, liveness_end, globalIndex],
  );

  const nodes = useNodes();
  const edges = useEdges();
  const incomingEdgeCount = React.useMemo(
    () => getIncomers({ id: name }, nodes, edges).length,
    [name, nodes, edges],
  );
  const outgoingEdgeCount = React.useMemo(
    () => getOutgoers({ id: name }, nodes, edges).length,
    [name, nodes, edges],
  );
  const { zoom } = useViewport();

  // Latest playhead position requested, and whether a render loop is draining.
  // Together these coalesce rapid scrub updates: while a frame is in flight,
  // newer indices just overwrite `latestIndexRef`, and the loop renders only
  // the most recent one rather than every intermediate position.
  const latestIndexRef = React.useRef(globalIndex);
  const renderingRef = React.useRef(false);

  React.useEffect(() => {
    latestIndexRef.current = globalIndex;

    if (renderingRef.current) {
      return;
    }

    async function render() {
      try {
        while (true) {
          const target = latestIndexRef.current;

          let buffer: ArrayBuffer;

          switch (visualizationMode) {
            case "True Values":
              buffer = await renderAt(name, target);
              break;
            case "Store Frequency":
            case "Load Frequency":
              buffer = await renderHeatmap(name, target, visualizationMode);
              break;
            case "Reuse Distance":
              buffer = await renderReuseDistance(name, target);
              break;
            case "Redundant Stores":
              buffer = await renderRedundant(name, target);
              break;
          }

          const ctx = canvasRef.current?.getContext("2d");

          if (ctx) {
            const pixels = new Uint8ClampedArray(buffer);
            ctx.putImageData(new ImageData(pixels, width, height), 0, 0);
          }

          if (latestIndexRef.current === target) {
            break;
          }
        }
      } catch {
        console.error(
          `Failed to render ${name} at index ${latestIndexRef.current}`,
        );
      }
    }

    render();
  }, [globalIndex, name, width, height, visualizationMode]);

  return (
    <div className="flex flex-col gap-1">
      <span className="text-ps-text-primary text-responsive truncate font-mono whitespace-nowrap uppercase">
        {name}
      </span>
      <div
        className={clsx({
          "ring-highlight/30": isFuncBufferLive,
          "ring-4": isFuncBufferLive && zoom < 1,
          "ring-2": isFuncBufferLive && zoom >= 1,
        })}
      >
        <canvas
          ref={canvasRef}
          width={width}
          height={height}
          className={clsx({
            "ring-highlight": isFuncBufferLive,
            "ring-2": isFuncBufferLive && zoom < 1,
            "ring-1": isFuncBufferLive && zoom >= 1,
          })}
        />
      </div>
      {incomingEdgeCount > 0 ? (
        <Handle
          type="target"
          position={Position.Left}
          className="bg-transparent"
          style={{ border: "none" }}
        >
          <HandleCircle />
        </Handle>
      ) : null}
      {outgoingEdgeCount > 0 ? (
        <Handle
          type="source"
          position={Position.Right}
          className="bg-transparent"
          style={{ border: "none" }}
        >
          <HandleCircle />
        </Handle>
      ) : null}
    </div>
  );
}

export default FuncCanvas;
