import * as d3 from "d3";
import { Slider } from "radix-ui";
import * as React from "react";

import { CanvasRegistry } from "../hooks/canvas-registry";
import { RangeRequest, RenderResponse } from "../types";
import {
  PLAYBACK_INTERVAL_MS,
  PLAYBACK_STEP,
  SCRUB_DEBOUNCE_MS,
  WS_ENDPOINT,
} from "../utils/constants";

interface TimelineProps {
  packetCount: number;
  sessionId: string;
  canvasRegistry: CanvasRegistry | null;
}

function Timeline({ packetCount, sessionId, canvasRegistry }: TimelineProps) {
  // Track the current packet index.
  const [packetIndex, setPacketIndex] = React.useState<number>(0);
  const [playing, setPlaying] = React.useState<boolean>(false);
  const wsRef = React.useRef<WebSocket | null>(null);

  // Use refs to synchronously track mutable state without triggering re-renders.
  const inFlightRef = React.useRef<boolean>(false);
  const timeRef = React.useRef<number>(0);
  const renderedEndRef = React.useRef<number>(0);
  const pendingEndRef = React.useRef<number>(0);
  const scrubTimerRef = React.useRef<number | null>(null);

  // Send the next range needed to bring the canvases to the current playhead.
  // Only one request is in flight at a time; on each response we pump again so
  // the canvases catch up to wherever the playhead has moved.
  const pump = React.useCallback(() => {
    if (
      inFlightRef.current ||
      !canvasRegistry ||
      !wsRef.current ||
      wsRef.current.readyState !== WebSocket.OPEN
    ) {
      console.warn("Skipping pump: ", {
        inFlight: inFlightRef.current,
        hasRegistry: !!canvasRegistry,
        wsRef: wsRef.current,
        readyState: wsRef.current?.readyState,
      });
      return;
    }

    const targetEnd = timeRef.current + 1;
    const rendered = renderedEndRef.current;

    if (targetEnd === rendered) {
      return;
    }

    let start: number;
    if (targetEnd < rendered) {
      // Backward: discard accumulated pixels and re-render from the start.
      canvasRegistry.clearAll();
      renderedEndRef.current = 0;
      start = 0;
    } else {
      // Forward: render only the new delta on top of the existing buffers.
      start = rendered;
    }

    // Send the request over the WebSocket.
    inFlightRef.current = true;
    pendingEndRef.current = targetEnd;
    const request: RangeRequest = { start, end: targetEnd };
    wsRef.current.send(JSON.stringify(request));
  }, [canvasRegistry]);

  // Scrub: track the playhead immediately, but defer rendering until the slider
  // settles so a drag doesn't fire a request per intermediate position.
  const onScrub = React.useCallback(
    (next: number) => {
      timeRef.current = next;
      setPacketIndex(next);

      if (scrubTimerRef.current !== null) {
        window.clearTimeout(scrubTimerRef.current);
      }

      scrubTimerRef.current = window.setTimeout(() => {
        scrubTimerRef.current = null;
        pump();
      }, SCRUB_DEBOUNCE_MS);
    },
    [pump],
  );

  const onTogglePlay = React.useCallback(() => {
    // Starting from the end replays from the beginning.
    if (!playing && timeRef.current >= packetCount - 1) {
      timeRef.current = 0;
      setPacketIndex(0);
    }

    setPlaying((p) => !p);
  }, [playing, packetCount]);

  React.useEffect(() => {
    if (!sessionId || !canvasRegistry) {
      return;
    }

    wsRef.current = new WebSocket(`${WS_ENDPOINT}/ws/${sessionId}`);

    wsRef.current.onopen = () => {
      pump();
    };

    wsRef.current.onmessage = (event: MessageEvent) => {
      const res: RenderResponse = JSON.parse(event.data);
      if (canvasRegistry) {
        for (const update of res.updates) {
          canvasRegistry.dispatch(update);
        }
      }

      if (res.done) {
        renderedEndRef.current = pendingEndRef.current;
        inFlightRef.current = false;
        pump();
      }
    };

    wsRef.current.onerror = (err: Event) =>
      console.error("WebSocket error:", err);
    wsRef.current.onclose = () => {};

    return () => {
      // Reset inFlight so a torn-down socket can't leave the next connection
      // permanently blocked on the guard in pump.
      inFlightRef.current = false;
      if (wsRef.current) {
        wsRef.current.close();
        wsRef.current.onopen = null;
        wsRef.current.onmessage = null;
        wsRef.current.onerror = null;
        wsRef.current.onclose = null;
        wsRef.current = null;
      }
    };
  }, [pump, canvasRegistry, sessionId]);

  // Playback loop: advance the playhead on a fixed interval and pump after each
  // step. Rendering may lag the playhead on large traces; it catches up via the
  // pump-on-response in the WebSocket handler.
  React.useEffect(() => {
    if (!playing) {
      return;
    }

    const id = window.setInterval(() => {
      const next = Math.min(timeRef.current + PLAYBACK_STEP, packetCount - 1);
      timeRef.current = next;
      setPacketIndex(next);
      pump();

      if (next >= packetCount - 1) {
        setPlaying(false);
      }
    }, PLAYBACK_INTERVAL_MS);

    return () => window.clearInterval(id);
  }, [packetCount, playing, pump]);

  const disabled = packetCount <= 0;
  const ticks = d3
    .ticks(0, packetCount - 1, 10)
    .filter((t) => t > 0 && t < packetCount - 1);

  return (
    <div className="bg-ps-primary border-t border-ps-border-primary px-4 flex items-center gap-2">
      <div className="flex gap-2 w-[85%] items-center h-full">
        <button
          onClick={onTogglePlay}
          disabled={disabled}
          className="px-2 py-1 my-6 rounded bg-ps-border-primary hover:bg-ps-border-primary/60 transition-colors disabled:opacity-40 disabled:cursor-not-allowed shrink-0 border border-ps-border-tertiary"
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
        <div className="relative flex-1 h-full py-2">
          {ticks.map((tick) => (
            <div
              key={tick}
              className="absolute top-2 bottom-2 border-l border-ps-text/30 border-dotted"
              style={{
                left: `${(tick / Math.max(packetCount - 1, 1)) * 100}%`,
              }}
            >
              <p className="text-xs text-ps-text pl-1">
                {d3.format(".2s")(tick)}
              </p>
            </div>
          ))}
          <Slider.Root
            className="relative top-1/2 -translate-y-1/2 flex items-center select-none touch-none w-full h-5"
            defaultValue={[0]}
            max={packetCount - 1}
            step={10000}
            onValueChange={(values) => onScrub(values[0])}
            value={[packetIndex]}
            disabled={disabled}
          >
            <Slider.Track className="relative flex-1 h-3 bg-ps-text rounded-sm border border-ps-text/30">
              <Slider.Range className="absolute bg-ps-border-primary h-full rounded-sm" />
            </Slider.Track>
            <Slider.Thumb
              className="h-5 w-6 rounded-sm cursor-pointer block bg-ps-text shadow-lg border border-ps-border-tertiary"
              aria-label="Volume"
            />
          </Slider.Root>
        </div>
      </div>
      <div className="flex flex-col items-end gap-0.5 text-sm w-[15%]">
        <span className="text-ps-text/60">Packets</span>
        <span className="text-ps-text">
          {packetIndex.toLocaleString()} /{" "}
          {Math.max(packetCount - 1, 0).toLocaleString()}
        </span>
      </div>
    </div>
  );
}

export default Timeline;
