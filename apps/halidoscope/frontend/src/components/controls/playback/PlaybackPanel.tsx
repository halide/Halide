import { useAtom } from "jotai";
import { RadioGroup } from "radix-ui";

import PlaybackLegend from "@/components/controls/playback/PlaybackLegend";
import { playbackModeAtom, type PlaybackMode } from "@/state/playback";

const PLAYBACK_MODES = [
  { value: "normal", label: "Normal" },
  { value: "stores", label: "Stores" },
  { value: "loads", label: "Loads" },
] as const;

function PlaybackPanel() {
  const [playbackMode, setPlaybackMode] = useAtom(playbackModeAtom);

  return (
    <div className="px-3 py-4 flex flex-col gap-4">
      <div className="flex flex-col gap-2">
        <label className="text-ps-text-primary uppercase font-semibold">
          Playback Mode
        </label>
        <RadioGroup.Root
          value={playbackMode}
          onValueChange={(value) => setPlaybackMode(value as PlaybackMode)}
          className="flex gap-3"
        >
          {PLAYBACK_MODES.map(({ value, label }) => (
            <div key={value} className="flex items-center gap-1">
              <RadioGroup.Item
                value={value}
                id={`playback-${value}`}
                className="w-3.5 h-3.5 rounded-full border border-ps-border-primary bg-ps-border-primary flex items-center justify-center focus:outline-none"
              >
                <RadioGroup.Indicator className="w-1.5 h-1.5 rounded-full bg-ps-text-primary" />
              </RadioGroup.Item>
              <label
                htmlFor={`playback-${value}`}
                className="text-ps-text-primary cursor-pointer"
              >
                {label}
              </label>
            </div>
          ))}
        </RadioGroup.Root>
      </div>
      {playbackMode !== "normal" ? (
        <PlaybackLegend playbackMode={playbackMode} />
      ) : null}
    </div>
  );
}

export default PlaybackPanel;
