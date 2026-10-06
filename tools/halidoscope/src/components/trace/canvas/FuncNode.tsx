import {
  getIncomers,
  getOutgoers,
  Handle,
  NodeToolbar,
  Position,
  useEdges,
  useNodes,
  useViewport,
  type Node,
  type NodeProps,
} from "@xyflow/react";
import { clsx } from "clsx";
import { useAtomValue, useSetAtom } from "jotai";
import * as React from "react";

import HandleCircle from "@/components/trace/canvas/HandleCircle";
import { funcAtom } from "@/state/func";
import { funcViewAtom, getFuncView, type FuncView } from "@/state/funcView";
import { hoverAtom } from "@/state/hover";
import { LIVENESS_COLORS, livenessAtom } from "@/state/liveness";
import { infAtom, nanAtom } from "@/state/nan-inf";
import { packetAtom } from "@/state/packet";
import { renderAtom } from "@/state/render";
import { tabularDataAtom } from "@/state/tabularData";
import { threadAtom } from "@/state/thread";
import type { FuncMeta, LiveBox } from "@/types/trace";
import {
  renderGrayscale,
  renderLoadFrequency,
  renderRedundantStores,
  renderReuseDistance,
  renderRgb,
  renderStoreFrequency,
  renderThread,
  type RenderFuncParams,
  type RenderFuncResponse,
} from "@/utils/api";
import { activeBoxes } from "@/utils/liveness";

interface LiveBoxRectProps {
  box: LiveBox;
  func: FuncMeta;
  view: FuncView;
  /** Whether dims 2 and up are sliced, so boxes not containing the slice are hidden. */
  showSlice: boolean;
  /** Whether dim 2 is displayed as color channels rather than sliced. */
  rgb: boolean;
  color: string;
  strokeWidth: number;
  /** How far outside the box the stroke's center lies, in stroke widths. */
  outset: number;
}

/** Outlines the region of a Func covered by a realize, produce, or consume node. */
function LiveBoxRect({
  box,
  func,
  view,
  showSlice,
  rgb,
  color,
  strokeWidth,
  outset,
}: LiveBoxRectProps) {
  const { bounds } = box;
  const dims = bounds.length / 2;
  if (showSlice) {
    for (let d = rgb ? 3 : 2; d < dims; d++) {
      const s = view.slice[d - 2];
      if (
        s !== undefined &&
        (s < bounds[2 * d] || s >= bounds[2 * d] + bounds[2 * d + 1])
      ) {
        return null;
      }
    }
  }
  const z = view.zoom;
  const [x, w] =
    dims > 0
      ? [(bounds[0] - func.min_coords[0]) * z, bounds[1] * z]
      : [0, func.width * z];
  const [y, h] =
    dims > 1
      ? [(bounds[2] - func.min_coords[1]) * z, bounds[3] * z]
      : [0, func.height * z];
  const o = outset * strokeWidth;
  return (
    <rect
      x={x - o}
      y={y - o}
      width={w + 2 * o}
      height={h + 2 * o}
      fill="none"
      stroke={color}
      strokeWidth={strokeWidth}
    />
  );
}

function FuncNode({ data }: NodeProps<Node<FuncMeta, "funcNode">>) {
  const { name, width, height, buffer_liveness, max_store_count } = data;
  const canvasRef = React.useRef<HTMLCanvasElement>(null);
  const nanOverlayRef = React.useRef<HTMLCanvasElement>(null);
  const infOverlayRef = React.useRef<HTMLCanvasElement>(null);

  const liveness = useAtomValue(livenessAtom);
  const packetIndex = useAtomValue(packetAtom);
  const render = useAtomValue(renderAtom);
  const activeFunc = useAtomValue(funcAtom);
  const setTabularData = useSetAtom(tabularDataAtom);
  const setHover = useSetAtom(hoverAtom);
  const nan = useAtomValue(nanAtom);
  const inf = useAtomValue(infAtom);
  const thread = useAtomValue(threadAtom);
  const views = useAtomValue(funcViewAtom);
  const modifiedView = views[name];
  const view = React.useMemo(
    () => getFuncView(modifiedView, data),
    [modifiedView, data],
  );

  const nodes = useNodes();
  const edges = useEdges();

  const active = activeFunc === name;
  const liveBoxes = React.useMemo(() => {
    if (!liveness.active) {
      return null;
    }
    return {
      realization: activeBoxes(data.liveness.realizations, packetIndex),
      production: activeBoxes(data.liveness.productions, packetIndex),
      consumption: activeBoxes(data.liveness.consumptions, packetIndex),
    };
  }, [liveness, data, packetIndex]);

  const incomingEdgeCount = React.useMemo(
    () => getIncomers({ id: name }, nodes, edges).length,
    [name, nodes, edges],
  );
  const outgoingEdgeCount = React.useMemo(
    () => getOutgoers({ id: name }, nodes, edges).length,
    [name, nodes, edges],
  );

  const { zoom } = useViewport();

  const displayStyle: React.CSSProperties = {
    width: width * view.zoom,
    height: height * view.zoom,
    imageRendering: "pixelated",
  };

  // Track the playhead position as a ref to avoid re-rendering on every scrub.
  const latestIndexRef = React.useRef(packetIndex);

  // Track whether an active render is in progress.
  const renderingRef = React.useRef(false);

  // Cache render responses if we are outside of a Func's liveness range.
  // In addition, add a useEffect call to invalidate the cache if any
  // application state affecting the RenderFuncResponse changes.
  const cachedPreLiveResultRef = React.useRef<RenderFuncResponse | null>(null);
  const cachedPostLiveResultRef = React.useRef<RenderFuncResponse | null>(null);

  React.useEffect(() => {
    cachedPreLiveResultRef.current = null;
    cachedPostLiveResultRef.current = null;
  }, [render, nan, inf, thread, active, view]);

  React.useEffect(() => {
    latestIndexRef.current = packetIndex;

    // Return early if we're actively writing a tensor.
    if (renderingRef.current) {
      return;
    }

    renderingRef.current = true;

    async function draw() {
      try {
        // Funcs with no stores (pipeline inputs) never see a Begin/EndRealization
        // pair, so `buffer_liveness` defaults to (0, 0) rather than a real teardown
        // point — treat those as always live instead of "dead" past index 0.
        const isRealized = max_store_count > 0;

        while (true) {
          const target = latestIndexRef.current;
          const notYetLive = target < buffer_liveness.start;
          const noLongerLive = isRealized && target > buffer_liveness.end;

          let result: RenderFuncResponse;

          if (notYetLive && cachedPreLiveResultRef.current) {
            result = cachedPreLiveResultRef.current;
          } else if (noLongerLive && cachedPostLiveResultRef.current) {
            result = cachedPostLiveResultRef.current;
          } else {
            const params: RenderFuncParams = {
              func: name,
              globalIndex: target,
              normalizationMode: render.normalizationMode,
              width,
              height,
              includeTabularData: active,
              includeNan: {
                active: nan.active,
                ...nan.color,
              },
              includeInf: {
                active: inf.active,
                ...inf.color,
              },
            };

            switch (render.renderMode) {
              case "Grayscale":
                result = await renderGrayscale({ ...params, view });
                break;
              case "RGB":
                result = await renderRgb({ ...params, view });
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

            // Cache the fetch if we fall outside the Func's buffer liveness range.
            if (notYetLive) {
              cachedPreLiveResultRef.current = result;
            } else if (noLongerLive) {
              cachedPostLiveResultRef.current = result;
            }
          }

          const ctx = canvasRef.current?.getContext("2d");
          if (ctx) {
            ctx.putImageData(
              new ImageData(result.tensorData, width, height),
              0,
              0,
            );
          }

          const nanCtx = nanOverlayRef.current?.getContext("2d");
          if (nanCtx && result.nanOverlayData) {
            nanCtx.putImageData(
              new ImageData(result.nanOverlayData, width, height),
              0,
              0,
            );
          }

          const infCtx = infOverlayRef.current?.getContext("2d");
          if (infCtx && result.infOverlayData) {
            infCtx.putImageData(
              new ImageData(result.infOverlayData, width, height),
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
      } finally {
        renderingRef.current = false;
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
    nan,
    inf,
    buffer_liveness.start,
    buffer_liveness.end,
    max_store_count,
    view,
  ]);

  return (
    <>
      <NodeToolbar
        isVisible
        position={Position.Top}
        align="start"
        offset={2}
        style={{ maxWidth: `${width * view.zoom * zoom}px` }}
        className="truncate"
      >
        <span
          className={clsx("text-ps-text-primary font-mono whitespace-nowrap", {
            "text-tiny": zoom < 0.5,
            "text-xs": zoom >= 0.5,
          })}
        >
          {name}
        </span>
      </NodeToolbar>
      <div className="relative">
        <canvas
          ref={canvasRef}
          width={width}
          height={height}
          style={displayStyle}
          onMouseMove={(e) => {
            const rect = e.currentTarget.getBoundingClientRect();
            const x = Math.floor(
              ((e.clientX - rect.left) / rect.width) * width,
            );
            const y = Math.floor(
              ((e.clientY - rect.top) / rect.height) * height,
            );
            setHover((prev) =>
              prev?.func === name && prev.x === x && prev.y === y
                ? prev
                : { func: name, x, y },
            );
          }}
          onMouseLeave={() => setHover(null)}
        />
        <canvas
          ref={nanOverlayRef}
          width={width}
          height={height}
          style={displayStyle}
          className={clsx("pointer-events-none absolute top-0 left-0", {
            hidden: !nan.active,
            "animate-blink": nan.active && nan.animationMode === "Blink",
            "animate-pulse": nan.active && nan.animationMode === "Pulse",
          })}
        />
        <canvas
          ref={infOverlayRef}
          width={width}
          height={height}
          style={displayStyle}
          className={clsx("pointer-events-none absolute top-0 left-0", {
            hidden: !inf.active,
            "animate-blink": inf.active && inf.animationMode === "Blink",
            "animate-pulse": inf.active && inf.animationMode === "Pulse",
          })}
        />
        {liveBoxes ? (
          <svg
            className="pointer-events-none absolute top-0 left-0 overflow-visible"
            width={width * view.zoom}
            height={height * view.zoom}
          >
            {(["realization", "production", "consumption"] as const).map(
              (kind) =>
                liveBoxes[kind].map((box, k) => (
                  <LiveBoxRect
                    key={`${kind}-${k}`}
                    box={box}
                    func={data}
                    view={view}
                    showSlice={
                      render.renderMode === "Grayscale" ||
                      render.renderMode === "RGB"
                    }
                    rgb={render.renderMode === "RGB"}
                    color={LIVENESS_COLORS[kind]}
                    strokeWidth={2 / zoom}
                    outset={kind === "realization" ? 1.5 : 0.5}
                  />
                )),
            )}
          </svg>
        ) : null}
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
