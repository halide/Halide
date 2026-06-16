import * as d3 from "d3";
import * as React from "react";

import { useTraceContext } from "@/hooks/trace";
import type { PlaybackMode } from "@/state/playback";

const RAMP_HEIGHT = 16;

function materializeColorRamp(
  interpolator: (t: number) => string,
  direction: "Forward" | "Reverse",
  n: number,
): string[] {
  const colors: string[] = [];

  for (let i = 0; i <= n; i++) {
    colors.push(
      d3
        .rgb(interpolator((direction === "Forward" ? i : n - i) / n))
        .formatHex(),
    );
  }

  return colors;
}

interface PlaybackLegendProps {
  playbackMode: PlaybackMode;
}

function PlaybackLegend({ playbackMode }: PlaybackLegendProps) {
  const canvas = React.useRef<HTMLCanvasElement>(null);
  const colors = materializeColorRamp(
    playbackMode === "stores" ? d3.interpolateReds : d3.interpolateBlues,
    "Forward",
    256,
  );
  const { globalMaxStoreCount, globalMaxLoadCount } = useTraceContext();

  const drawRamp = React.useCallback(
    (ctx: CanvasRenderingContext2D) => {
      ctx.clearRect(0, 0, canvas.current!.width, canvas.current!.height);

      for (let i = 0; i < colors.length; ++i) {
        ctx.fillStyle = colors[i];
        ctx.fillRect(i, 0, 1, RAMP_HEIGHT);
      }
    },
    [colors],
  );

  React.useEffect(() => {
    if (canvas.current) {
      const ctx = canvas.current?.getContext("2d");

      canvas.current.style.width = "100%";
      canvas.current.style.height = `${RAMP_HEIGHT}px`;

      if (ctx) {
        drawRamp(ctx);
      }
    }
  }, [drawRamp]);

  return (
    <div className="flex flex-col gap-1 h-4 w-full text-tiny text-ps-text-primary">
      <span>
        {playbackMode === "stores" ? "Store Count →" : "Load Count →"}
      </span>
      <canvas ref={canvas} width={colors.length} height={RAMP_HEIGHT}></canvas>
      <div className="flex justify-between">
        <span>0</span>
        <span>
          {playbackMode === "stores" ? globalMaxStoreCount : globalMaxLoadCount}
        </span>
      </div>
    </div>
  );
}

export default PlaybackLegend;
