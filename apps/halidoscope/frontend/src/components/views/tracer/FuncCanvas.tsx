import {
  Handle,
  Position,
  type Node,
  type NodeProps,
  useViewport,
} from "@xyflow/react";
import * as d3 from "d3";
import { useAtom } from "jotai";
import * as React from "react";

import HandleCircle from "@/components/shared/HandleCircle";
import { useCanvasRegistry } from "@/hooks/canvas-registry";
import type { FuncStats, ChannelData } from "@/types";
import { useTraceContext } from "@/hooks/trace";
import { playbackModeAtom } from "@/state/playback";

type FuncNode = Node<FuncStats, "funcCanvas">;

function FuncCanvas({
  id,
  data: { name, width, height },
}: NodeProps<FuncNode>) {
  const canvasRef = React.useRef<HTMLCanvasElement>(null);
  const canvasRegistry = useCanvasRegistry();
  const [playbackMode] = useAtom(playbackModeAtom);
  const { globalMaxStoreCount, globalMaxLoadCount } = useTraceContext();
  const { zoom } = useViewport();

  const storeScale = React.useMemo(
    () =>
      d3.scaleSequential(d3.interpolateReds).domain([0, globalMaxStoreCount]),
    [globalMaxStoreCount],
  );

  const loadScale = React.useMemo(
    () =>
      d3.scaleSequential(d3.interpolateBlues).domain([0, globalMaxLoadCount]),
    [globalMaxLoadCount],
  );

  const applyChannel = React.useCallback(
    (imageData: ImageDataArray, ch: ChannelData, offset: number) => {
      for (let i = 0; i < ch.xs.length; i++) {
        imageData[4 * (ch.ys[i] * width + ch.xs[i]) + offset] = ch.values[i];
      }
    },
    [width],
  );

  const drawNormal = React.useCallback(
    ({
      xs,
      ys,
      values,
      r,
      g,
      b,
      image,
      ctx,
    }: {
      xs?: number[];
      ys?: number[];
      values?: number[];
      r?: ChannelData;
      g?: ChannelData;
      b?: ChannelData;
      image: ImageData;
      ctx: CanvasRenderingContext2D;
    }) => {
      const data = image.data;

      if (r) {
        applyChannel(data, r, 0);
      }

      if (g) {
        applyChannel(data, g, 1);
      }

      if (b) {
        applyChannel(data, b, 2);
      }

      if (xs && ys && values) {
        for (let i = 0; i < xs.length; i++) {
          const idx = 4 * (ys[i] * width + xs[i]);
          const v = values[i];
          data[idx] = v;
          data[idx + 1] = v;
          data[idx + 2] = v;
          data[idx + 3] = 255;
        }
      }

      ctx.putImageData(image, 0, 0);
    },
    [applyChannel, width],
  );

  const drawStoreCounts = React.useCallback(
    ({
      xs,
      ys,
      counts,
      storeCountBuf,
      image,
      ctx,
    }: {
      xs?: number[];
      ys?: number[];
      counts?: number[];
      storeCountBuf: Int32Array;
      image: ImageData;
      ctx: CanvasRenderingContext2D;
    }) => {
      if (!xs || !ys) return;

      const data = image.data;

      for (let i = 0; i < xs.length; i++) {
        const idx = ys[i] * width + xs[i];
        storeCountBuf[idx] += counts ? counts[i] : 1;
        const { r, g, b } = d3.color(storeScale(storeCountBuf[idx]))!.rgb();
        data[4 * idx] = r;
        data[4 * idx + 1] = g;
        data[4 * idx + 2] = b;
        data[4 * idx + 3] = 255;
      }

      ctx.putImageData(image, 0, 0);
    },
    [storeScale, width],
  );

  const drawLoadCounts = React.useCallback(
    ({
      xs,
      ys,
      counts,
      loadCountBuf,
      image,
      ctx,
    }: {
      xs?: number[];
      ys?: number[];
      counts?: number[];
      loadCountBuf: Int32Array;
      image: ImageData;
      ctx: CanvasRenderingContext2D;
    }) => {
      if (!xs || !ys) return;

      const data = image.data;

      for (let i = 0; i < xs.length; i++) {
        const idx = ys[i] * width + xs[i];
        loadCountBuf[idx] += counts ? counts[i] : 1;
        const { r, g, b } = d3.color(loadScale(loadCountBuf[idx]))!.rgb();
        data[4 * idx] = r;
        data[4 * idx + 1] = g;
        data[4 * idx + 2] = b;
        data[4 * idx + 3] = 255;
      }

      ctx.putImageData(image, 0, 0);
    },
    [loadScale, width],
  );

  React.useEffect(() => {
    const ctx = canvasRef.current?.getContext("2d");
    if (!ctx) return;

    const image = ctx.createImageData(width, height);
    const storeCountBuf = new Int32Array(width * height);
    const loadCountBuf = new Int32Array(width * height);

    const reset = () => {
      const data = image.data;

      data.fill(0);
      // Opaque black background.
      for (let i = 3; i < data.length; i += 4) data[i] = 255;
      ctx.putImageData(image, 0, 0);

      // Reset the store/load count buffers.
      storeCountBuf.fill(0);
      loadCountBuf.fill(0);
    };

    reset();

    const unregister = canvasRegistry.register(id, {
      draw: ({ xs, ys, values, counts, r, g, b }) => {
        switch (playbackMode) {
          case "normal":
            drawNormal({ xs, ys, values, r, g, b, image, ctx });
            break;
          case "stores":
            drawStoreCounts({ xs, ys, counts, storeCountBuf, image, ctx });
            break;
          case "loads":
            drawLoadCounts({ xs, ys, counts, loadCountBuf, image, ctx });
            break;
        }
      },
      clear: reset,
    });

    return unregister;
  }, [
    id,
    width,
    height,
    canvasRegistry,
    applyChannel,
    drawNormal,
    drawStoreCounts,
    drawLoadCounts,
    playbackMode,
  ]);

  return (
    <div className="flex flex-col gap-2">
      <span className="font-mono uppercase text-ps-text-primary text-responsive whitespace-nowrap">
        {name}
      </span>
      <canvas ref={canvasRef} width={width} height={height} />
      <Handle
        type="target"
        position={Position.Left}
        className="bg-none border-none"
      >
        <HandleCircle zoom={zoom} />
      </Handle>
      <Handle
        type="source"
        position={Position.Right}
        className="bg-none border-none"
      >
        <HandleCircle zoom={zoom} />
      </Handle>
    </div>
  );
}

export default FuncCanvas;
