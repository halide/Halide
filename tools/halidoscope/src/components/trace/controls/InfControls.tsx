import * as d3 from "d3";
import { useAtom } from "jotai";

import ColorInput from "@/components/trace/controls/ColorInput";
import Checkbox from "@/components/shared/Checkbox";
import Select from "@/components/shared/Select";
import {
  DEFAULT_INF_COLOR,
  infAtom,
  ANIMATION_MODES,
  type AnimationMode,
} from "@/state/nan-inf";

function InfControls() {
  const [inf, setInf] = useAtom(infAtom);

  return (
    <div className="flex flex-col gap-4">
      <Checkbox
        checked={inf.active}
        id="highlight-inf-checkbox"
        label="Highlight Inf Values"
        onCheckedChange={(active) => setInf({ ...inf, active })}
      />
      {inf.active ? (
        <div className="ml-4 grid grid-cols-[minmax(0,0.4fr)_minmax(0,0.6fr)] gap-2">
          <Select
            id="inf-animation-mode-select"
            label="Animation"
            value={inf.animationMode}
            onValueChange={(value) =>
              setInf({ ...inf, animationMode: value as AnimationMode })
            }
            items={ANIMATION_MODES.map((mode) => ({
              value: mode,
              label: mode,
            }))}
          />
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
