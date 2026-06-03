import * as React from "react";

interface FuncCanvasProps {
  name: string;
  width: number;
  height: number;
  xs: number[];
  ys: number[];
  values: number[];
}

const MAX_DISPLAY_PX = 800;

function FuncCanvas({ name, width, height, xs, ys, values }: FuncCanvasProps) {
  const canvas = React.useRef<HTMLCanvasElement>(null);
  const scale = Math.min(1, MAX_DISPLAY_PX / width, MAX_DISPLAY_PX / height);

  React.useEffect(() => {
    const ctx = canvas.current?.getContext("2d");

    if (ctx) {
      const imageData = ctx.createImageData(width, height);
      for (let i = 0; i < imageData.data.length; i += 4) {
        imageData.data[i + 3] = 255;
      }

      ctx.putImageData(imageData, 0, 0);
    }

    // if (ctx) {
    //   const imageData = ctx.createImageData(width, height);

    //   for (let i = 0; i < xs.length; i++) {
    //     const idx = 4 * (ys[i] * width + xs[i]);
    //     imageData.data[idx + 0] = values[i]; // R
    //     imageData.data[idx + 1] = values[i]; // G
    //     imageData.data[idx + 2] = values[i]; // B
    //     imageData.data[idx + 3] = 255;
    //   }

    //   ctx.putImageData(imageData, 0, 0);
    // }
  }, [xs, ys, values, width, height]);

  return (
    <div className="flex flex-col gap-1">
      <span className="text-xs font-mono uppercase text-slate-400">
        {name}
        {scale < 1 && (
          <span className="ml-2 text-slate-500">
            {Math.round(scale * 100)}%
          </span>
        )}
      </span>
      <canvas
        ref={canvas}
        width={width}
        height={height}
        style={{ width: width * scale, height: height * scale }}
        className="border border-slate-600"
      ></canvas>
    </div>
  );
}

export default FuncCanvas;
