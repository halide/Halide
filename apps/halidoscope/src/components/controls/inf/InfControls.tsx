import * as d3 from "d3";
import { useAtom } from "jotai";
import { Checkbox, Label, Select } from "radix-ui";

import ColorInput from "@/components/controls/color/ColorInput";
import ArrowDownIcon from "@/components/icons/ArrowDownIcon";
import CheckIcon from "@/components/icons/CheckIcon";
import { DEFAULT_INF_COLOR, infAtom } from "@/state/inf";
import type { AnimationMode } from "@/types";
import { ANIMATION_MODES } from "@/utils/constants";

function InfControls() {
  const [inf, setInf] = useAtom(infAtom);

  return (
    <div className="flex flex-col gap-4">
      <div className="flex items-center gap-2">
        <Checkbox.Root
          className="bg-ps-border-primary border-ps-border-tertiary flex h-4 w-4 items-center justify-center rounded-xs border"
          checked={inf.active}
          onCheckedChange={(checked) => {
            setInf({ ...inf, active: !!checked });
          }}
        >
          <Checkbox.Indicator>
            <CheckIcon />
          </Checkbox.Indicator>
        </Checkbox.Root>
        <label className="text-ps-text-primary/60">Highlight Inf Values</label>
      </div>
      {inf.active ? (
        <div className="ml-4 grid grid-cols-[minmax(0,0.4fr)_minmax(0,0.6fr)] gap-2">
          <div className="flex flex-col gap-1">
            <Label.Root
              className="text-ps-text-primary/60"
              htmlFor="inf-animation-mode-select"
            >
              Animation
            </Label.Root>
            <Select.Root
              value={inf.animationMode}
              onValueChange={(value) =>
                setInf({ ...inf, animationMode: value as AnimationMode })
              }
            >
              <Select.Trigger
                id="inf-animation-mode-select"
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
          <ColorInput
            id="inf-color"
            defaultValue={DEFAULT_INF_COLOR}
            color={d3.rgb(inf.color.r, inf.color.g, inf.color.b).formatHex()}
            onChangeColor={(color) => {
              const { r, g, b } = d3.color(color)?.rgb() ?? {
                r: 0,
                g: 0,
                b: 0,
              };
              setInf({ ...inf, color: { ...inf.color, r, g, b } });
            }}
            alpha={Math.round(inf.color.a * 100)}
            onChangeAlpha={(alpha) => {
              setInf({
                ...inf,
                color: { ...inf.color, a: alpha / 100 },
              });
            }}
          />
        </div>
      ) : null}
    </div>
  );
}

export default InfControls;
