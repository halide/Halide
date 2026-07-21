import { useAtom } from "jotai";
import { Checkbox, Label, Select } from "radix-ui";

import ArrowDownIcon from "@/components/icons/ArrowDownIcon";
import CheckIcon from "@/components/icons/CheckIcon";
import { nanAtom } from "@/state/nan";
import type { AnimationMode } from "@/types";
import { ANIMATION_MODES } from "@/utils/constants";

function NaNControls() {
  const [nan, setNan] = useAtom(nanAtom);

  return (
    <div className="flex flex-col gap-4">
      <div className="flex items-center gap-2">
        <Checkbox.Root
          className="bg-ps-border-primary border-ps-border-tertiary flex h-4 w-4 items-center justify-center rounded-xs border"
          checked={nan.active}
          onCheckedChange={(checked) => {
            setNan({ ...nan, active: !!checked });
          }}
        >
          <Checkbox.Indicator>
            <CheckIcon />
          </Checkbox.Indicator>
        </Checkbox.Root>
        <label className="text-ps-text-primary/60">Highlight NaN Values</label>
      </div>
      {nan.active ? (
        <div className="ml-4 flex flex-col gap-1">
          <Label.Root
            className="text-ps-text-primary/60"
            htmlFor="animation-mode-select"
          >
            Animation Mode
          </Label.Root>
          <Select.Root
            value={nan.animationMode}
            onValueChange={(value) =>
              setNan({ ...nan, animationMode: value as AnimationMode })
            }
          >
            <Select.Trigger
              id="animation-mode-select"
              className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary inline-flex h-8 w-full items-center justify-center rounded border px-2 focus:outline-none"
            >
              <Select.Value className="text-ps-text-primary" />
              <Select.Icon className="ml-auto">
                <ArrowDownIcon />
              </Select.Icon>
            </Select.Trigger>
            <Select.Content
              position="popper"
              sideOffset={4}
              className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary z-10 max-h-(--radix-select-content-available-height) w-(--radix-select-trigger-width) rounded border"
            >
              <Select.Viewport>
                {ANIMATION_MODES.map((value) => (
                  <Select.Item
                    key={value}
                    value={value}
                    className="hover:bg-ps-border-tertiary cursor-pointer p-2 transition-colors"
                  >
                    <Select.ItemText>{value}</Select.ItemText>
                  </Select.Item>
                ))}
              </Select.Viewport>
            </Select.Content>
          </Select.Root>
        </div>
      ) : null}
    </div>
  );
}

export default NaNControls;
