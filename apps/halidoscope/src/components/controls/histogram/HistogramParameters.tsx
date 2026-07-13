import { Label, Select } from "radix-ui";
import { useAtom } from "jotai";

import ArrowDownIcon from "@/components/icons/ArrowDownIcon";
import { useTraceContext } from "@/hooks/trace";
import { funcAtom } from "@/state/func";
import { histogramAtom, type HistogramScale } from "@/state/histogram";
import { type NormalizationMode, renderAtom } from "@/state/render";

function HistogramSelect() {
  const { funcs } = useTraceContext();
  const [activeFunc, setActiveFunc] = useAtom(funcAtom);
  const [histogram, setHistogram] = useAtom(histogramAtom);
  const [render, setRender] = useAtom(renderAtom);

  return (
    <div className="flex flex-col gap-2">
      <div className="flex flex-col gap-1">
        <Label.Root className="text-ps-text-primary/60" htmlFor="func-select">
          Selected Func
        </Label.Root>
        <Select.Root
          value={activeFunc}
          onValueChange={(value) => {
            setActiveFunc(value);
            setHistogram({ ...histogram, data: null });
          }}
        >
          <Select.Trigger
            id="func-select"
            className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary inline-flex h-8 w-full items-center justify-center rounded border px-2 uppercase focus:outline-none"
          >
            <span className="truncate">
              <Select.Value />
            </span>
            <Select.Icon className="ml-auto">
              <svg
                xmlns="http://www.w3.org/2000/svg"
                fill="none"
                viewBox="0 0 24 24"
                strokeWidth={1.5}
                stroke="currentColor"
                className="h-3 w-3"
              >
                <path
                  strokeLinecap="round"
                  strokeLinejoin="round"
                  d="M19.5 8.25l-7.5 7.5-7.5-7.5"
                />
              </svg>
            </Select.Icon>
          </Select.Trigger>
          <Select.Content
            position="popper"
            sideOffset={4}
            className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary z-10 max-h-(--radix-select-content-available-height) w-(--radix-select-trigger-width) rounded border"
          >
            <Select.Viewport>
              {Object.keys(funcs).map((func) => (
                <Select.Item
                  key={func}
                  value={func}
                  className="hover:bg-ps-border-tertiary cursor-pointer p-2 uppercase transition-colors"
                >
                  <Select.ItemText className="uppercase">
                    {func}
                  </Select.ItemText>
                </Select.Item>
              ))}
            </Select.Viewport>
          </Select.Content>
        </Select.Root>
      </div>
      <div className="grid grid-cols-2 gap-2">
        <div className="flex flex-col gap-1">
          <Label.Root
            className="text-ps-text-primary/60"
            htmlFor="scale-select"
          >
            Scale
          </Label.Root>
          <Select.Root
            value={histogram.scale}
            onValueChange={(value) =>
              setHistogram({ ...histogram, scale: value as HistogramScale })
            }
          >
            <Select.Trigger
              id="scale-select"
              className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary inline-flex h-8 w-full items-center justify-center rounded border px-2 focus:outline-none"
            >
              <Select.Value />
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
                <Select.Item
                  key="linear"
                  value="linear"
                  className="hover:bg-ps-border-tertiary cursor-pointer p-2 transition-colors"
                >
                  <Select.ItemText>Linear</Select.ItemText>
                </Select.Item>
                <Select.Item
                  key="log"
                  value="log"
                  className="hover:bg-ps-border-tertiary cursor-pointer p-2 transition-colors"
                >
                  <Select.ItemText>Log</Select.ItemText>
                </Select.Item>
              </Select.Viewport>
            </Select.Content>
          </Select.Root>
        </div>
        <div className="flex flex-col gap-1">
          <Label.Root
            className="text-ps-text-primary/60"
            htmlFor="normalization-select"
          >
            Normalize Display
          </Label.Root>
          <Select.Root
            value={render.normalizationMode}
            onValueChange={(value) =>
              setRender({
                ...render,
                normalizationMode: value as NormalizationMode,
              })
            }
          >
            <Select.Trigger
              id="normalization-select"
              className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary inline-flex h-8 w-full items-center justify-center rounded border px-2 focus:outline-none"
            >
              <Select.Value />
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
                <Select.Item
                  key="across-funcs"
                  value="Across Funcs"
                  className="hover:bg-ps-border-tertiary cursor-pointer p-2 transition-colors"
                >
                  <Select.ItemText>Across Funcs</Select.ItemText>
                </Select.Item>
                <Select.Item
                  key="per-func"
                  value="Per Func"
                  className="hover:bg-ps-border-tertiary cursor-pointer p-2 transition-colors"
                >
                  <Select.ItemText>Per Func</Select.ItemText>
                </Select.Item>
              </Select.Viewport>
            </Select.Content>
          </Select.Root>
        </div>
      </div>
    </div>
  );
}

export default HistogramSelect;
