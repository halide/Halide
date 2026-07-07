import {
  getIncomers,
  getOutgoers,
  Handle,
  type Node,
  type NodeProps,
  NodeToolbar,
  Position,
  useEdges,
  useNodes,
  useViewport,
} from "@xyflow/react";
import clsx from "clsx";
import { useAtomValue } from "jotai";
import * as React from "react";

import HandleCircle from "@/components/shared/HandleCircle";
import { livenessAtom } from "@/state/liveness";
import { packetAtom } from "@/state/packet";
import { renderModeAtom } from "@/state/render";
import type { FuncMeta } from "@/types";
import {
  renderGrayscale,
  renderRgb,
  renderStoreFrequency,
  renderLoadFrequency,
  renderRedundantStores,
  renderReuseDistance,
} from "@/utils/api";
import {
  isFuncBufferLive,
  isFuncConsuming,
  isFuncProducing,
} from "@/utils/liveness";

type FuncNode = Node<FuncMeta, "funcCanvas">;

function FuncCanvas({ data }: NodeProps<FuncNode>) {
  const { name, width, height } = data;
  const canvasRef = React.useRef<HTMLCanvasElement>(null);

  const livenessMode = useAtomValue(livenessAtom);
  const packetIndex = useAtomValue(packetAtom);
  const renderMode = useAtomValue(renderModeAtom);

  const bufferLive = React.useMemo(
    () =>
      livenessMode === "realizations" && isFuncBufferLive(data, packetIndex),
    [livenessMode, data, packetIndex],
  );
  const producing = React.useMemo(
    () =>
      livenessMode === "produce-consume" && isFuncProducing(data, packetIndex),
    [livenessMode, data, packetIndex],
  );
  const consuming = React.useMemo(
    () =>
      livenessMode === "produce-consume" && isFuncConsuming(data, packetIndex),
    [livenessMode, data, packetIndex],
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
  const latestIndexRef = React.useRef(packetIndex);
  const renderingRef = React.useRef(false);

  React.useEffect(() => {
    latestIndexRef.current = packetIndex;

    if (renderingRef.current) {
      return;
    }

    async function render() {
      try {
        while (true) {
          const target = latestIndexRef.current;

          let buffer: ArrayBuffer;

          switch (renderMode) {
            case "Grayscale":
              buffer = await renderGrayscale(name, target);
              break;
            case "RGB":
              buffer = await renderRgb(name, target);
              break;
            case "Store Frequency":
              buffer = await renderStoreFrequency(name, target);
              break;
            case "Load Frequency":
              buffer = await renderLoadFrequency(name, target);
              break;
            case "Reuse Distance":
              buffer = await renderReuseDistance(name, target);
              break;
            case "Redundant Stores":
              buffer = await renderRedundantStores(name, target);
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
  }, [packetIndex, name, width, height, renderMode]);

  return (
    <>
      <NodeToolbar isVisible position={Position.Top} align="start" offset={2}>
        <span
          className={clsx(
            "text-ps-text-primary truncate font-mono whitespace-nowrap uppercase",
            {
              "text-tiny": zoom < 0.5,
              "text-xs": zoom >= 0.5 && zoom < 1.5,
              "text-sm": zoom >= 1.5,
            },
          )}
        >
          {name}
        </span>
      </NodeToolbar>
      <div
        className={clsx("ring-transparent", {
          "ring-highlight/30!": bufferLive,
          "ring-produce/30!": producing,
          "ring-consume/30!": consuming,
          "ring-4": zoom < 1,
          "ring-2": zoom >= 1,
        })}
      >
        <canvas
          ref={canvasRef}
          width={width}
          height={height}
          className={clsx("ring-transparent", {
            "ring-highlight!": bufferLive,
            "ring-produce!": producing,
            "ring-consume!": consuming,
            "ring-2": zoom < 1,
            "ring-1": zoom >= 1,
          })}
        />
      </div>
      {incomingEdgeCount > 0 && edges.every((edge) => !edge.hidden) ? (
        <Handle
          type="target"
          position={Position.Left}
          className="bg-transparent"
          style={{ border: "none" }}
        >
          <HandleCircle />
        </Handle>
      ) : null}
      {outgoingEdgeCount > 0 && edges.every((edge) => !edge.hidden) ? (
        <Handle
          type="source"
          position={Position.Right}
          className="bg-transparent"
          style={{ border: "none" }}
        >
          <HandleCircle />
        </Handle>
      ) : null}
    </>
  );
}

export default FuncCanvas;
