import { useAtom } from "jotai";
import { RadioGroup } from "radix-ui";

import { livenessAtom, type LivenessMode } from "@/state/liveness";

function LivenessControls() {
  const [livenessMode, setLivenessMode] = useAtom(livenessAtom);

  return (
    <RadioGroup.Root
      className="flex flex-col gap-2"
      value={livenessMode}
      onValueChange={(value) => setLivenessMode(value as LivenessMode)}
    >
      <div className="flex items-center gap-2">
        <RadioGroup.Item
          className="bg-ps-border-primary border-ps-border-tertiary h-4 w-4 rounded-full border"
          value="none"
          id="liveness-none"
        >
          <RadioGroup.Indicator className="after:bg-ps-text-primary relative flex h-full w-full items-center justify-center after:block after:h-2 after:w-2 after:rounded-full after:content-['']" />
        </RadioGroup.Item>
        <label className="text-ps-text-primary/60" htmlFor="liveness-none">
          Highlight Nothing
        </label>
      </div>
      <div className="flex items-center gap-2">
        <RadioGroup.Item
          className="bg-ps-border-primary border-ps-border-tertiary h-4 w-4 rounded-full border"
          value="realizations"
          id="liveness-realizations"
        >
          <RadioGroup.Indicator className="after:bg-ps-text-primary relative flex h-full w-full items-center justify-center after:block after:h-2 after:w-2 after:rounded-full after:content-['']" />
        </RadioGroup.Item>
        <label
          className="text-ps-text-primary/60"
          htmlFor="liveness-realizations"
        >
          Highlight Realizations
        </label>
      </div>
      <div className="flex items-center gap-2">
        <RadioGroup.Item
          className="bg-ps-border-primary border-ps-border-tertiary h-4 w-4 rounded-full border"
          value="produce-consume"
          id="liveness-produce-consume"
        >
          <RadioGroup.Indicator className="after:bg-ps-text-primary relative flex h-full w-full items-center justify-center after:block after:h-2 after:w-2 after:rounded-full after:content-['']" />
        </RadioGroup.Item>
        <label
          className="text-ps-text-primary/60"
          htmlFor="liveness-produce-consume"
        >
          Highlight Produce-Consume
        </label>
      </div>
    </RadioGroup.Root>
  );
}

export default LivenessControls;
