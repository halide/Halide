import * as React from "react";
import type { Node, NodeProps } from "@xyflow/react";

import { useCanvasRegistry } from "../hooks/canvas-registry";
import { NodeData } from "../types";

type FuncNode = Node<NodeData, "funcCanvas">;

/**
 * Renders a single Halide func's store values into a canvas. The canvas owns a
 * persistent {@link ImageData} buffer and registers a draw/clear handle with the
 * {@link CanvasRegistry}, so incremental range updates accumulate on top of each
 * other (forward scrub) until a clear resets it (backward scrub / new trace).
 */
function FuncCanvas({
  id,
  data: { name, width, height },
}: NodeProps<FuncNode>) {
  const canvasRef = React.useRef<HTMLCanvasElement>(null);
  const canvasRegistry = useCanvasRegistry();

  React.useEffect(() => {
    const ctx = canvasRef.current?.getContext("2d");
    if (!ctx) return;

    const image = ctx.createImageData(width, height);

    const reset = () => {
      const data = image.data;
      data.fill(0);
      // Opaque black background.
      for (let i = 3; i < data.length; i += 4) data[i] = 255;
      ctx.putImageData(image, 0, 0);
    };

    reset();

    const unregister = canvasRegistry.register(id, {
      draw: ({ xs, ys, values }) => {
        const data = image.data;
        for (let i = 0; i < xs.length; i++) {
          const idx = 4 * (ys[i] * width + xs[i]);
          const v = values[i];
          data[idx] = v;
          data[idx + 1] = v;
          data[idx + 2] = v;
          data[idx + 3] = 255;
        }
        ctx.putImageData(image, 0, 0);
      },
      clear: reset,
    });

    return unregister;
  }, [id, width, height, canvasRegistry]);

  return (
    <div className="flex flex-col gap-1">
      <span className="text-sm font-mono uppercase text-ps-text">{name}</span>
      <canvas ref={canvasRef} width={width} height={height} />
    </div>
  );
}

export default FuncCanvas;
