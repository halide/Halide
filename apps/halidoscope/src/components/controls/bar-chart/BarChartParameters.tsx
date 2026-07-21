import { Label, Select } from "radix-ui";
import { useAtom } from "jotai";

import ArrowDownIcon from "@/components/icons/ArrowDownIcon";
import { useTraceContext } from "@/hooks/trace";
import { funcAtom } from "@/state/func";
import { threadAtom } from "@/state/thread";

function BarChartParameters() {
  const { funcs } = useTraceContext();
  const [activeFunc, setActiveFunc] = useAtom(funcAtom);
  const [thread, setThread] = useAtom(threadAtom);

  return (
    <div className="grid grid-cols-2 gap-2">
      <div className="flex flex-col gap-1">
        <Label.Root className="text-ps-text-primary/60" htmlFor="func-select">
          Selected Func
        </Label.Root>
        <Select.Root
          value={activeFunc}
          onValueChange={(value) => {
            setActiveFunc(value);
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
              <ArrowDownIcon />
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
      <div className="flex flex-col gap-1">
        <Label.Root className="text-ps-text-primary/60" htmlFor="op-select">
          Operation
        </Label.Root>
        <Select.Root
          value={thread.op}
          onValueChange={(value) => {
            setThread({ ...thread, op: value as "Load" | "Store" });
          }}
        >
          <Select.Trigger
            id="op-select"
            className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary inline-flex h-8 w-full items-center justify-center rounded border px-2 focus:outline-none"
          >
            <span className="truncate">
              <Select.Value />
            </span>
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
                key="Store"
                value="Store"
                className="hover:bg-ps-border-tertiary cursor-pointer p-2 transition-colors"
              >
                <Select.ItemText>Store</Select.ItemText>
              </Select.Item>
              <Select.Item
                key="Load"
                value="Load"
                className="hover:bg-ps-border-tertiary cursor-pointer p-2 transition-colors"
              >
                <Select.ItemText>Load</Select.ItemText>
              </Select.Item>
            </Select.Viewport>
          </Select.Content>
        </Select.Root>
      </div>
    </div>
  );
}

export default BarChartParameters;
