import {
  getIncomers,
  getOutgoers,
  Handle,
  Position,
  type Node,
  type NodeProps,
  useEdges,
  useNodes,
} from "@xyflow/react";
import { useAtomValue } from "jotai";
import * as React from "react";

import HandleCircle from "@/components/shared/HandleCircle";
import type { FuncMeta } from "@/types";
import { packetAtom } from "@/state/packet";
import { visualizationModeAtom } from "@/state/visualization";
import { renderAt, renderHeatmap, renderRedundant } from "@/utils/api";

type FuncNode = Node<FuncMeta, "funcCanvas">;

function FuncCanvas({ data: { name, width, height } }: NodeProps<FuncNode>) {
  const canvasRef = React.useRef<HTMLCanvasElement>(null);
  const globalIndex = useAtomValue(packetAtom);
  const visualizationMode = useAtomValue(visualizationModeAtom);

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

  // Latest playhead position requested, and whether a render loop is draining.
  // Together these coalesce rapid scrub updates: while a frame is in flight,
  // newer indices just overwrite `latestIndexRef`, and the loop renders only
  // the most recent one rather than every intermediate position.
  const latestIndexRef = React.useRef(globalIndex);
  const renderingRef = React.useRef(false);

  const paint = React.useCallback(async () => {
    if (renderingRef.current) {
      return;
    }

    renderingRef.current = true;
    try {
      // Drain to the latest requested index, skipping any that arrived while a
      // previous frame was rendering.
      while (true) {
        const target = latestIndexRef.current;
        const buffer = await renderAt(name, target);

        const ctx = canvasRef.current?.getContext("2d");

        if (ctx) {
          const pixels = new Uint8ClampedArray(buffer);
          ctx.putImageData(new ImageData(pixels, width, height), 0, 0);
        }

        if (latestIndexRef.current === target) {
          break;
        }
      }
    } catch (err) {
      console.error(`Failed to render ${name}:`, err);
    } finally {
      renderingRef.current = false;
    }
  }, [name, width, height]);

  React.useEffect(() => {
    if (visualizationMode !== "True Values") return;
    latestIndexRef.current = globalIndex;
    paint();
  }, [globalIndex, visualizationMode, paint]);

  React.useEffect(() => {
    if (
      visualizationMode !== "Store Frequency" &&
      visualizationMode !== "Load Frequency"
    )
      return;
    renderHeatmap(name, globalIndex, visualizationMode)
      .then((buffer) => {
        const ctx = canvasRef.current?.getContext("2d");
        if (ctx) {
          ctx.putImageData(
            new ImageData(new Uint8ClampedArray(buffer), width, height),
            0,
            0,
          );
        }
      })
      .catch((err) =>
        console.error(`Failed to render heatmap for ${name}:`, err),
      );
  }, [globalIndex, visualizationMode, name, width, height]);

  React.useEffect(() => {
    if (visualizationMode !== "Redundant Stores") return;
    renderRedundant(name, globalIndex)
      .then((buffer) => {
        const ctx = canvasRef.current?.getContext("2d");
        if (ctx) {
          ctx.putImageData(
            new ImageData(new Uint8ClampedArray(buffer), width, height),
            0,
            0,
          );
        }
      })
      .catch((err) =>
        console.error(`Failed to render redundant for ${name}:`, err),
      );
  }, [globalIndex, visualizationMode, name, width, height]);

  return (
    <div className="flex flex-col gap-2">
      <span className="text-ps-text-primary text-responsive font-mono whitespace-nowrap uppercase">
        {name}
      </span>
      <canvas ref={canvasRef} width={width} height={height} />
      {incomingEdgeCount > 0 ? (
        <Handle
          type="target"
          position={Position.Left}
          className="border-none bg-none"
        >
          <HandleCircle />
        </Handle>
      ) : null}
      {outgoingEdgeCount > 0 ? (
        <Handle
          type="source"
          position={Position.Right}
          className="border-none bg-none"
        >
          <HandleCircle />
        </Handle>
      ) : null}
    </div>
  );
}

export default FuncCanvas;
