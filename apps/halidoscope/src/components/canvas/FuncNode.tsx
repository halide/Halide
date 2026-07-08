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
import { clsx } from "clsx";
import { useAtomValue } from "jotai";
import * as React from "react";

import HandleCircle from "@/components/canvas/HandleCircle";
import { useTraceContext } from "@/hooks/trace";
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
import { isFuncBufferLive, isEdgeLive } from "@/utils/liveness";

function FuncNode({ data }: NodeProps<Node<FuncMeta, "funcNode">>) {
  const { name, width, height } = data;
  const canvasRef = React.useRef<HTMLCanvasElement>(null);

  const { funcs } = useTraceContext();
  const liveness = useAtomValue(livenessAtom);
  const packetIndex = useAtomValue(packetAtom);
  const renderMode = useAtomValue(renderModeAtom);

  const nodes = useNodes();
  const edges = useEdges();

  const bufferLive = React.useMemo(
    () =>
      liveness.active &&
      liveness.mode === "realizations" &&
      isFuncBufferLive(data, packetIndex),
    [liveness, data, packetIndex],
  );

  const producing = React.useMemo(
    () =>
      liveness.active &&
      liveness.mode === "produce-consume" &&
      edges.some(
        (edge) =>
          edge.source === name &&
          isEdgeLive(funcs, edge.source, edge.target, packetIndex),
      ),
    [liveness, edges, funcs, name, packetIndex],
  );

  const consuming = React.useMemo(
    () =>
      liveness.active &&
      liveness.mode === "produce-consume" &&
      edges.some(
        (edge) =>
          edge.target === name &&
          isEdgeLive(funcs, edge.source, edge.target, packetIndex),
      ),
    [liveness, edges, funcs, name, packetIndex],
  );

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
            case "Redundant Stores":
              buffer = await renderRedundantStores(name, target);
              break;
            case "Reuse Distance":
              buffer = await renderReuseDistance(name, target);
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
      <NodeToolbar
        isVisible
        position={Position.Top}
        align="start"
        offset={2}
        style={{ maxWidth: `${width * zoom}px` }}
        className="truncate"
      >
        <span
          className={clsx(
            "text-ps-text-primary font-mono whitespace-nowrap uppercase",
            {
              "text-tiny": zoom < 0.5,
              "text-xs": zoom >= 0.5,
            },
          )}
        >
          {name}
        </span>
      </NodeToolbar>
      <div
        className={clsx("ring-transparent", {
          "ring-realization/30!": bufferLive,
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
            "ring-realization!": bufferLive,
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

export default FuncNode;
