import { useAtom } from "jotai";
import { Label, Slider } from "radix-ui";

import { useTraceContext } from "@/hooks/trace";
import { playbackRateAtom, playbackRateFor } from "@/state/playback";

function PlaybackRate() {
  const [playbackRate, setPlaybackRate] = useAtom(playbackRateAtom);
  const { packetCount } = useTraceContext();
  const maxRate = Math.max(playbackRate, playbackRateFor(packetCount, 0.5));

  return (
    <div className="flex flex-col gap-1">
      <Label.Root className="text-ps-text-primary/60">
        Playback Rate (Packets / Tick)
      </Label.Root>
      <div className="flex items-center gap-2">
        <Slider.Root
          min={1}
          max={maxRate}
          step={1}
          value={[playbackRate]}
          onValueChange={(value) => setPlaybackRate(value[0])}
          className="relative flex h-4 flex-1 items-center"
        >
          <Slider.Track className="bg-ps-text-primary border-ps-border-tertiary relative h-2 flex-1 rounded-xs border">
            <Slider.Range className="bg-ps-border-primary absolute h-full" />
          </Slider.Track>
          <Slider.Thumb
            className="bg-ps-text-primary border-ps-border-tertiary block h-3 w-3 cursor-pointer rounded-xs border shadow-lg"
            aria-label="Playback Rate"
          />
        </Slider.Root>
        <input
          type="number"
          className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary w-20 appearance-none rounded-sm border px-2 py-1.75 text-xs"
          value={playbackRate}
          onChange={(e) => setPlaybackRate(Number(e.target.value))}
          min={1}
          step={1}
        />
      </div>
    </div>
  );
}

export default PlaybackRate;
