import * as d3 from "d3";
import { useAtom, useSetAtom } from "jotai";
import { Slider } from "radix-ui";
import * as React from "react";

import { packetAtom } from "@/state/packet";
import { playbackRateAtom } from "@/state/playback";
import { SCRUB_DEBOUNCE_MS } from "@/utils/constants";

interface TracerTimelineProps {
  packetCount: number;
}

function TracerTimeline({ packetCount }: TracerTimelineProps) {
  // Local slider position, for a smooth thumb independent of render cadence.
  const [packetIndex, setPacketIndex] = React.useState<number>(0);
  const [playing, setPlaying] = React.useState<boolean>(false);
  const [playbackRate] = useAtom(playbackRateAtom);
  const setGlobalIndex = useSetAtom(packetAtom);

  // Mirror the latest index synchronously for the playback interval closure.
  const indexRef = React.useRef<number>(0);
  const scrubTimerRef = React.useRef<number | null>(null);

  const commitIndex = React.useCallback((next: number) => {
    indexRef.current = next;
    setPacketIndex(next);
  }, []);

  // Scrub: move the thumb immediately, but defer the (debounced) render so a
  // drag doesn't fire a request per intermediate position. FuncCanvases
  // coalesce in flight, but debouncing still trims redundant render cycles.
  const onScrub = React.useCallback(
    (next: number) => {
      commitIndex(next);

      if (scrubTimerRef.current !== null) {
        window.clearTimeout(scrubTimerRef.current);
      }

      scrubTimerRef.current = window.setTimeout(() => {
        scrubTimerRef.current = null;
        setGlobalIndex(next);
      }, SCRUB_DEBOUNCE_MS);
    },
    [commitIndex, setGlobalIndex],
  );

  const onTogglePlay = React.useCallback(() => {
    // Starting from the end replays from the beginning.
    if (!playing && indexRef.current >= packetCount - 1) {
      commitIndex(0);
      setGlobalIndex(0);
    }

    setPlaying((p) => !p);
  }, [playing, packetCount, commitIndex, setGlobalIndex]);

  // Playback loop: advance the playhead on a fixed interval and push each step
  // straight to the global index. Canvases coalesce if rendering lags.
  React.useEffect(() => {
    if (!playing) {
      return;
    }

    let animationFrameId: number | null = null;

    function step() {
      const next = Math.min(indexRef.current + playbackRate, packetCount - 1);
      commitIndex(next);
      setGlobalIndex(next);

      if (next >= packetCount - 1 && animationFrameId !== null) {
        setPlaying(false);
        cancelAnimationFrame(animationFrameId);
      }

      animationFrameId = requestAnimationFrame(step);
    }

    step();

    return () => {
      if (animationFrameId !== null) {
        cancelAnimationFrame(animationFrameId);
      }
    };
  }, [packetCount, playing, commitIndex, setGlobalIndex, playbackRate]);

  const disabled = packetCount <= 0;
  const ticks = d3
    .ticks(0, packetCount - 1, 10)
    .filter((t) => t > 0 && t < packetCount - 1);

  return (
    <div className="bg-ps-primary border-ps-border-primary flex items-center gap-2 border-t px-4">
      <div className="flex h-full w-[85%] items-center gap-2">
        <button
          onClick={onTogglePlay}
          disabled={disabled}
          className="bg-ps-border-primary hover:bg-ps-border-primary/60 border-ps-border-tertiary my-6 shrink-0 rounded border px-2 py-1 transition-colors disabled:cursor-not-allowed disabled:opacity-40"
          aria-label={playing ? "Pause" : "Play"}
        >
          {playing ? (
            <svg
              xmlns="http://www.w3.org/2000/svg"
              width="16"
              height="16"
              viewBox="0 0 24 24"
              fill="currentColor"
            >
              <rect x="6" y="4" width="4" height="16" rx="1" />
              <rect x="14" y="4" width="4" height="16" rx="1" />
            </svg>
          ) : (
            <svg
              xmlns="http://www.w3.org/2000/svg"
              width="16"
              height="16"
              viewBox="0 0 24 24"
              fill="currentColor"
            >
              <polygon points="5,3 19,12 5,21" />
            </svg>
          )}
        </button>
        <div className="relative h-full flex-1 py-2">
          {ticks.map((tick) => (
            <div
              key={tick}
              className="border-ps-text-primary/30 absolute top-2 bottom-2 border-l border-dotted"
              style={{
                left: `${(tick / Math.max(packetCount - 1, 1)) * 100}%`,
              }}
            >
              <p className="text-ps-text-primary pl-1 text-xs">
                {d3.format(".2s")(tick)}
              </p>
            </div>
          ))}
          <Slider.Root
            className="relative top-1/2 flex h-5 w-full -translate-y-1/2 touch-none items-center select-none"
            defaultValue={[0]}
            max={packetCount - 1}
            step={playbackRate}
            onValueChange={(values) => onScrub(values[0])}
            value={[packetIndex]}
            disabled={disabled}
          >
            <Slider.Track className="bg-ps-text-primary border-ps-text-primary/30 relative h-3 flex-1 rounded-sm border">
              <Slider.Range className="bg-ps-border-primary absolute h-full rounded-sm" />
            </Slider.Track>
            <Slider.Thumb
              className="bg-ps-text-primary border-ps-border-tertiary block h-5 w-6 cursor-pointer rounded-sm border shadow-lg"
              aria-label="Playhead"
            />
          </Slider.Root>
        </div>
      </div>
      <div className="flex w-[15%] flex-col items-end gap-0.5 text-sm">
        <span className="text-ps-text-primary/60">Packets</span>
        <span className="text-ps-text-primary">
          {packetIndex.toLocaleString()} /{" "}
          {Math.max(packetCount - 1, 0).toLocaleString()}
        </span>
      </div>
    </div>
  );
}

export default TracerTimeline;
