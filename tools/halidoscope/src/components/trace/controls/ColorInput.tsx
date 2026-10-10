import * as d3 from "d3";
import { Label } from "radix-ui";
import * as React from "react";

interface Props {
  id: string;
  color: string;
  defaultValue: string;
  onChangeColor: (color: string) => void;
  alpha: number;
  onChangeAlpha: (opacity: number) => void;
}

const hexPattern = /^#([0-9A-Fa-f]{3}){1,2}$/i;

function ColorInput({
  id,
  color,
  defaultValue,
  onChangeColor,
  alpha,
  onChangeAlpha,
}: Props) {
  const [colorLocal, setColorLocal] = React.useState<string>(color);
  const [alphaLocal, setAlphaLocal] = React.useState<number>(alpha);

  const onChange = React.useCallback(
    (event: React.ChangeEvent<HTMLInputElement>) => {
      let output = event.currentTarget.value;
      setColorLocal(output);

      if (!output.startsWith("#")) {
        output = "#" + output;
      }

      if (hexPattern.test(output)) {
        onChangeColor(d3.color(output)?.formatHex() ?? defaultValue);
      }
    },
    [defaultValue, onChangeColor],
  );

  const onBlur = React.useCallback(() => {
    let c = color;

    if (!color.startsWith("#")) {
      c = "#" + c;
    }

    if (!hexPattern.test(c)) {
      setColorLocal(defaultValue);
      onChangeColor(defaultValue);
    }
  }, [color, onChangeColor, defaultValue]);

  const onChangeAlphaLocal = React.useCallback(
    (event: React.ChangeEvent<HTMLInputElement>) => {
      const output = Number(event.currentTarget.value);
      setAlphaLocal(output);

      if (output >= 0 && output <= 100) {
        onChangeAlpha(output);
      }
    },
    [setAlphaLocal, onChangeAlpha],
  );

  const onBlurAlphaLocal = React.useCallback(() => {
    const a = alphaLocal;

    if (Number.isNaN(a)) {
      setAlphaLocal(alpha);
    } else if (a < 0) {
      setAlphaLocal(0);
      onChangeAlpha(0);
    } else if (a > 100) {
      setAlphaLocal(100);
      onChangeAlpha(100);
    }
  }, [alpha, alphaLocal, onChangeAlpha]);

  return (
    <div className="flex flex-col gap-1">
      <Label.Root htmlFor={id} className="text-ps-text-primary/60">
        Color
      </Label.Root>
      <div className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary flex h-8 items-center gap-1 rounded border px-2">
        <input
          type="color"
          className="h-4 w-4 shrink-0 cursor-pointer appearance-none rounded-sm"
          value={color}
          onChange={onChange}
        />
        <input
          type="text"
          id={id}
          value={colorLocal}
          onChange={onChange}
          onBlur={onBlur}
          className="border-ps-border-tertiary ml-1 h-full min-w-0 flex-1 border-r"
        />
        <input
          type="number"
          min={0}
          max={100}
          step={1}
          value={alphaLocal}
          className="ml-1 w-6"
          onChange={onChangeAlphaLocal}
          onBlur={onBlurAlphaLocal}
        />
        <span className="text-ps-text-primary/60">%</span>
      </div>
    </div>
  );
}

export default ColorInput;
