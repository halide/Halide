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
import { histogramAtom } from "@/state/histogram";
import { infAtom } from "@/state/inf";
import { livenessAtom } from "@/state/liveness";
import { packetAtom } from "@/state/packet";
import { nanAtom } from "@/state/nan";
import { renderAtom } from "@/state/render";
import { threadAtom } from "@/state/thread";
import type { FuncMeta } from "@/types";
import {
  renderGrayscale,
  renderRgb,
  renderStoreFrequency,
  renderLoadFrequency,
  renderRedundantStores,
  renderReuseDistance,
  type RenderResult,
  renderNaN,
  renderInf,
  renderThread,
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
  const setHistogramData = useSetAtom(histogramAtom);
  const nan = useAtomValue(nanAtom);
  const inf = useAtomValue(infAtom);
  const thread = useAtomValue(threadAtom);

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

    async function draw() {
      try {
        while (true) {
          const target = latestIndexRef.current;

          let result: RenderResult;

          switch (render.renderMode) {
            case "Grayscale":
              result = await renderGrayscale(
                name,
                target,
                render.normalizationMode,
              );
              break;
            case "RGB":
              result = await renderRgb(name, target, render.normalizationMode);
              break;
            case "Store Frequency":
              result = await renderStoreFrequency(
                name,
                target,
                render.normalizationMode,
                width,
                height,
              );
              break;
            case "Load Frequency":
              result = await renderLoadFrequency(
                name,
                target,
                render.normalizationMode,
                width,
                height,
              );
              break;
            case "Redundant Stores":
              result = await renderRedundantStores(
                name,
                target,
                render.normalizationMode,
                width,
                height,
              );
              break;
            case "Reuse Distance":
              result = await renderReuseDistance(
                name,
                target,
                render.normalizationMode,
                width,
                height,
              );
              break;
            case "Thread Coverage":
              result = await renderThread(
                name,
                target,
                render.normalizationMode,
                thread.op,
                width,
                height,
              );

              break;
          }

          const ctx = canvasRef.current?.getContext("2d");

          // Update this Func's canvas with new pixel data.
          if (ctx) {
            ctx.putImageData(new ImageData(result.pixels, width, height), 0, 0);
          }

          // Update the histogram data for the currently active Func.
          if (name === activeFunc) {
            setHistogramData((prev) => ({ ...prev, data: result.histogram }));
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
    packetIndex,
    name,
    width,
    height,
    render,
    activeFunc,
    setHistogramData,
    thread.op,
  ]);

  React.useEffect(() => {
    if (!nan.active) {
      return;
    }

    latestIndexRef.current = packetIndex;

    if (renderingRef.current) {
      return;
    }

    async function drawNaN() {
      try {
        while (true) {
          const target = latestIndexRef.current;

          const result = await renderNaN(
            name,
            packetIndex,
            render.normalizationMode,
          );

          const ctx = nanOverlayRef.current?.getContext("2d");

          if (ctx) {
            ctx.putImageData(new ImageData(result.pixels, width, height), 0, 0);
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
    if (!inf.active) {
      return;
    }

    latestIndexRef.current = packetIndex;

    if (renderingRef.current) {
      return;
    }

    async function drawInf() {
      try {
        while (true) {
          const target = latestIndexRef.current;

          const result = await renderInf(
            name,
            packetIndex,
            render.normalizationMode,
          );

          const ctx = infOverlayRef.current?.getContext("2d");

          if (ctx) {
            ctx.putImageData(new ImageData(result.pixels, width, height), 0, 0);
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
