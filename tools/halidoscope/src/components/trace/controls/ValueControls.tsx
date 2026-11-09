import { Label, Slider } from "radix-ui";
import * as React from "react";

import { useFuncView } from "@/hooks/funcView";
import { maxZoom } from "@/state/funcView";
import type { FuncMeta } from "@/types/trace";

interface Props {
  func: FuncMeta;
}

function LevelInput({
  id,
  label,
  value,
  onCommit,
}: {
  id: string;
  label: string;
  value: number;
  onCommit: (value: number) => void;
}) {
  // The text being edited, or null when the input isn't being edited.
  const [text, setText] = React.useState<string | null>(null);

  return (
    <div className="flex flex-col gap-1">
      <Label.Root className="text-ps-text-primary/60" htmlFor={id}>
        {label}
      </Label.Root>
      <input
        id={id}
        type="text"
        inputMode="decimal"
        className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary h-8 rounded border px-2 font-mono"
        value={text ?? String(value)}
        onChange={(e) => {
          setText(e.target.value);
          const parsed = Number(e.target.value);
          if (e.target.value.trim() !== "" && Number.isFinite(parsed)) {
            onCommit(parsed);
          }
        }}
        onBlur={() => setText(null)}
      />
    </div>
  );
}

function LabeledSlider({
  label,
  min,
  max,
  value,
  onChange,
}: {
  label: string;
  min: number;
  max: number;
  value: number;
  onChange: (value: number) => void;
}) {
  return (
    <div className="flex flex-col gap-1">
      <Label.Root className="text-ps-text-primary/60">
        {label}: {value}
      </Label.Root>
      <Slider.Root
        min={min}
        max={max}
        step={1}
        value={[value]}
        onValueChange={([v]) => onChange(v)}
        className="relative flex h-4 flex-1 items-center"
      >
        <Slider.Track className="bg-ps-text-primary border-ps-border-tertiary relative h-2 flex-1 rounded-xs border">
          <Slider.Range className="bg-ps-border-primary absolute h-full" />
        </Slider.Track>
        <Slider.Thumb
          className="bg-ps-text-primary border-ps-border-tertiary block h-3 w-3 cursor-pointer rounded-xs border shadow-lg"
          aria-label={label}
        />
      </Slider.Root>
    </div>
  );
}

/** The dims from `firstSliderDim` on with more than one coordinate, and their inclusive ranges. */
export function sliceSliderDims(func: FuncMeta, firstSliderDim: number) {
  return func.min_coords
    .map((min, d) => ({ d, min, max: func.max_coords[d] - 1 }))
    .filter(({ d, min, max }) => d >= firstSliderDim && max > min);
}

interface SliceSlidersProps {
  func: FuncMeta;
  /** The first logical dim that gets a slice slider. */
  firstSliderDim: number;
}

/** A slider for each non-image dim of a Func with more than one coordinate, selecting the slice displayed. */
export function SliceSliders({ func, firstSliderDim }: SliceSlidersProps) {
  const [view, update] = useFuncView(func);

  return (
    <div className="flex flex-col gap-2">
      {sliceSliderDims(func, firstSliderDim).map(({ d, min, max }) => (
        <LabeledSlider
          key={d}
          label={`Dimension ${d}`}
          min={min}
          max={max}
          value={view.slice[d - 2]}
          onChange={(c) => {
            const slice = [...view.slice];
            slice[d - 2] = c;
            update({ slice });
          }}
        />
      ))}
    </div>
  );
}

function ValueControls({ func }: Props) {
  const [view, update] = useFuncView(func);

  return (
    <div className="flex flex-col gap-2">
      <div className="grid grid-cols-2 gap-2">
        <LevelInput
          id="black-point-input"
          label="Black Point"
          value={view.blackPoint}
          onCommit={(blackPoint) => update({ blackPoint })}
        />
        <LevelInput
          id="white-point-input"
          label="White Point"
          value={view.whitePoint}
          onCommit={(whitePoint) => update({ whitePoint })}
        />
      </div>
      <LabeledSlider
        label="Zoom"
        min={1}
        max={maxZoom(func)}
        value={view.zoom}
        onChange={(zoom) => update({ zoom })}
      />
    </div>
  );
}

export default ValueControls;
