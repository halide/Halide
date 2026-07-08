import { useAtom } from "jotai";
import { Checkbox, RadioGroup } from "radix-ui";

import CheckIcon from "@/components/icons/CheckIcon";
import { livenessAtom, type LivenessMode } from "@/state/liveness";

function LivenessControls() {
  const [liveness, setLiveness] = useAtom(livenessAtom);

  return (
    <div className="flex flex-col gap-2">
      <div className="flex items-center gap-2">
        <Checkbox.Root
          className="bg-ps-border-primary border-ps-border-tertiary flex h-4 w-4 items-center justify-center rounded-xs border"
          checked={liveness.active}
          onCheckedChange={(checked) => {
            setLiveness({ ...liveness, active: !!checked });
          }}
        >
          <Checkbox.Indicator>
            <CheckIcon />
          </Checkbox.Indicator>
        </Checkbox.Root>
        <label className="text-ps-text-primary/60">Highlight Liveness</label>
      </div>
      {liveness.active ? (
        <RadioGroup.Root
          className="ml-4 flex flex-col gap-2"
          value={liveness.mode}
          onValueChange={(value) =>
            setLiveness({ ...liveness, mode: value as LivenessMode })
          }
        >
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
              Realizations
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
              Producer-Consumer Relationships
            </label>
          </div>
        </RadioGroup.Root>
      ) : null}
    </div>
  );
}

export default LivenessControls;
