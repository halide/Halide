import * as d3 from "d3";
import { useAtom } from "jotai";

import ColorInput from "@/components/controls/color/ColorInput";
import Checkbox from "@/components/shared/Checkbox";
import Select from "@/components/shared/Select";
import {
  DEFAULT_NAN_COLOR,
  nanAtom,
  ANIMATION_MODES,
  type AnimationMode,
} from "@/state/nan-inf";

function NaNControls() {
  const [nan, setNan] = useAtom(nanAtom);

  return (
    <div className="flex flex-col gap-4">
      <Checkbox
        checked={nan.active}
        id="highlight-nan-checkbox"
        label="Highlight NaN Values"
        onCheckedChange={(active) => setNan({ ...nan, active })}
      />
      {nan.active ? (
        <div className="ml-4 grid grid-cols-[minmax(0,0.4fr)_minmax(0,0.6fr)] gap-2">
          <Select
            id="animation-mode-select"
            label="Animation"
            value={nan.animationMode}
            onValueChange={(value) =>
              setNan({ ...nan, animationMode: value as AnimationMode })
            }
            items={ANIMATION_MODES.map((mode) => ({
              value: mode,
              label: mode,
            }))}
          />
          <ColorInput
            id="nan-color"
            defaultValue={DEFAULT_NAN_COLOR}
            color={d3.rgb(nan.color.r, nan.color.g, nan.color.b).formatHex()}
            onChangeColor={(color) => {
              const { r, g, b } = d3.color(color)?.rgb() ?? {
                r: 0,
                g: 0,
                b: 0,
              };
              setNan({ ...nan, color: { ...nan.color, r, g, b } });
            }}
            alpha={Math.round(nan.color.a * 100)}
            onChangeAlpha={(alpha) => {
              setNan({
                ...nan,
                color: { ...nan.color, a: alpha / 100 },
              });
            }}
          />
        </div>
      ) : null}
    </div>
  );
}

export default NaNControls;
