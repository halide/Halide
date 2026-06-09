import * as React from "react";
import type { Node, NodeProps } from "@xyflow/react";

import { useCanvasRegistry } from "../hooks/canvas-registry";
import { NodeData, ChannelData } from "../types";

type FuncNode = Node<NodeData, "funcCanvas">;

function FuncCanvas({
  id,
  data: { name, width, height },
}: NodeProps<FuncNode>) {
  const canvasRef = React.useRef<HTMLCanvasElement>(null);
  const canvasRegistry = useCanvasRegistry();

  const applyChannel = React.useCallback(
    (imageData: ImageDataArray, ch: ChannelData, offset: number) => {
      for (let i = 0; i < ch.xs.length; i++) {
        imageData[4 * (ch.ys[i] * width + ch.xs[i]) + offset] = ch.values[i];
      }
    },
    [width],
  );

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
      draw: ({ xs, ys, values, r, g, b }) => {
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
      clear: reset,
    });

    return unregister;
  }, [id, width, height, canvasRegistry, applyChannel]);

  return (
    <div className="flex flex-col gap-2">
      <span className="font-mono uppercase text-ps-text text-responsive whitespace-nowrap">
        {name}
      </span>
      <canvas ref={canvasRef} width={width} height={height} />
    </div>
  );
}

export default FuncCanvas;
