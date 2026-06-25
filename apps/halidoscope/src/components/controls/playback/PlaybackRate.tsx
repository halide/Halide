import { useAtom } from "jotai";
import { Label, Slider } from "radix-ui";

import { DEFAULT_PLAYBACK_RATE } from "@/utils/constants";
import { playbackRateAtom } from "@/state/playback";

const MIN_RATE = 100;
const MAX_RATE = 20_000;
const STEP = 100;

function PlaybackRate() {
  const [playbackRate, setPlaybackRate] = useAtom(playbackRateAtom);

  return (
    <div className="flex flex-col gap-1">
      <Label.Root className="text-ps-text-primary/60">
        Playback Rate (Packets / Tick)
      </Label.Root>
      <div className="flex items-center gap-2">
        <Slider.Root
          defaultValue={[DEFAULT_PLAYBACK_RATE]}
          min={MIN_RATE}
          max={MAX_RATE}
          step={STEP}
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
          className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary w-14 appearance-none rounded-sm border px-2 py-1.75 text-xs"
          value={playbackRate}
          onChange={(e) => setPlaybackRate(Number(e.target.value))}
          min={MIN_RATE}
          max={MAX_RATE}
          step={STEP}
        />
      </div>
    </div>
  );
}

export default PlaybackRate;
