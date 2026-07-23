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
import { useAtomValue, useSetAtom } from "jotai";
import * as React from "react";

import HandleCircle from "@/components/canvas/HandleCircle";
import { useTraceContext } from "@/hooks/trace";
import { funcAtom } from "@/state/func";
import { infAtom } from "@/state/inf";
import { livenessAtom } from "@/state/liveness";
import { packetAtom } from "@/state/packet";
import { nanAtom } from "@/state/nan";
import { renderAtom } from "@/state/render";
import { tabularDataAtom } from "@/state/tabularData";
import { threadAtom } from "@/state/thread";
import type { FuncMeta } from "@/types";
import {
  renderGrayscale,
  renderRgb,
  renderStoreFrequency,
  renderLoadFrequency,
  renderRedundantStores,
  renderReuseDistance,
  renderNaN,
  renderInf,
  renderThread,
  type RenderFuncParams,
  type RenderFuncResponse,
} from "@/utils/api";
import { isFuncBufferLive, isEdgeLive } from "@/utils/liveness";

function FuncNode({ data }: NodeProps<Node<FuncMeta, "funcNode">>) {
  const { name, width, height } = data;
  const canvasRef = React.useRef<HTMLCanvasElement>(null);
  const nanOverlayRef = React.useRef<HTMLCanvasElement>(null);
  const infOverlayRef = React.useRef<HTMLCanvasElement>(null);

  const { funcs } = useTraceContext();
  const liveness = useAtomValue(livenessAtom);
  const packetIndex = useAtomValue(packetAtom);
  const render = useAtomValue(renderAtom);
  const activeFunc = useAtomValue(funcAtom);
  const setTabularData = useSetAtom(tabularDataAtom);
  const nan = useAtomValue(nanAtom);
  const inf = useAtomValue(infAtom);
  const thread = useAtomValue(threadAtom);

  const nodes = useNodes();
  const edges = useEdges();

  const active = activeFunc === name;
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

  // Track the playhead position as a ref to avoid re-rendering on every scrub.
  const latestIndexRef = React.useRef(packetIndex);
  // Track whether an active render is in progress.
  const renderingRef = React.useRef(false);

  React.useEffect(() => {
    latestIndexRef.current = packetIndex;

    // Return early if we're actively writing a tensor.
    if (renderingRef.current) {
      return;
    }

    async function draw() {
      try {
        while (true) {
          const target = latestIndexRef.current;

          let result: RenderFuncResponse;
          const params: RenderFuncParams = {
            func: name,
            globalIndex: target,
            normalizationMode: render.normalizationMode,
            width,
            height,
            includeTabularData: active,
          };

          switch (render.renderMode) {
            case "Grayscale":
              result = await renderGrayscale(params);
              break;
            case "RGB":
              result = await renderRgb(params);
              break;
            case "Store Frequency":
              result = await renderStoreFrequency(params);
              break;
            case "Load Frequency":
              result = await renderLoadFrequency(params);
              break;
            case "Redundant Stores":
              result = await renderRedundantStores(params);
              break;
            case "Reuse Distance":
              result = await renderReuseDistance(params);
              break;
            case "Thread Coverage":
              result = await renderThread({
                ...params,
                threadOpMode: thread.op,
                threadId: thread.id,
              });

              break;
          }

          const ctx = canvasRef.current?.getContext("2d");
          if (ctx) {
            ctx.putImageData(
              new ImageData(result.tensorData, width, height),
              0,
              0,
            );
          }

          // Update the histogram data for the currently active Func.
          if (active) {
            setTabularData((prev) => ({
              ...prev,
              tabularData: result.tabularData,
            }));
          }

          if (latestIndexRef.current === target) {
            break;
          }
        }
      } catch (err) {
        console.error(
          `Failed to render ${name} at index ${latestIndexRef.current}: ${err}`,
        );
      }
    }

    draw();
  }, [
    active,
    packetIndex,
    name,
    width,
    height,
    render,
    activeFunc,
    setTabularData,
    thread,
  ]);

  React.useEffect(() => {
    latestIndexRef.current = packetIndex;

    if (!nan.active || renderingRef.current) {
      return;
    }

    async function drawNaN() {
      try {
        while (true) {
          const target = latestIndexRef.current;

          const result = await renderNaN({
            func: name,
            globalIndex: packetIndex,
            normalizationMode: render.normalizationMode,
            width,
            height,
            includeTabularData: false,
          });

          const ctx = nanOverlayRef.current?.getContext("2d");

          if (ctx) {
            ctx.putImageData(
              new ImageData(result.tensorData, width, height),
              0,
              0,
            );
          }

          if (latestIndexRef.current === target) {
            break;
          }
        }
      } catch {
        console.error(
          `Failed to render NaN overlay for ${name} at index ${latestIndexRef.current}`,
        );
      }
    }

    drawNaN();
  }, [nan.active, name, packetIndex, render.normalizationMode, width, height]);

  React.useEffect(() => {
    latestIndexRef.current = packetIndex;

    if (!inf.active || renderingRef.current) {
      return;
    }

    async function drawInf() {
      try {
        while (true) {
          const target = latestIndexRef.current;

          const result = await renderInf({
            func: name,
            globalIndex: packetIndex,
            normalizationMode: render.normalizationMode,
            width,
            height,
            includeTabularData: false,
          });

          const ctx = infOverlayRef.current?.getContext("2d");
          if (ctx) {
            ctx.putImageData(
              new ImageData(result.tensorData, width, height),
              0,
              0,
            );
          }

          if (latestIndexRef.current === target) {
            break;
          }
        }
      } catch {
        console.error(
          `Failed to render Inf overlay for ${name} at index ${latestIndexRef.current}`,
        );
      }
    }

    drawInf();
  }, [inf.active, name, packetIndex, render.normalizationMode, width, height]);

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
        className={clsx("relative ring-transparent", {
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
        <canvas
          ref={nanOverlayRef}
          width={width}
          height={height}
          className={clsx("absolute top-0 left-0", {
            hidden: !nan.active,
            "animate-blink": nan.active && nan.animationMode === "Blink",
            "animate-pulse": nan.active && nan.animationMode === "Pulse",
          })}
        />
        <canvas
          ref={infOverlayRef}
          width={width}
          height={height}
          className={clsx("absolute top-0 left-0", {
            hidden: !inf.active,
            "animate-blink": inf.active && inf.animationMode === "Blink",
            "animate-pulse": inf.active && inf.animationMode === "Pulse",
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
