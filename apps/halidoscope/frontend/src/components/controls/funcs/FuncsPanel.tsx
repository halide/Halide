import { useAtom } from "jotai";
import { Accordion } from "radix-ui";

import type { FuncMeta } from "@/types";
import { funcAtom } from "@/state/func";

interface FuncsPanelProps {
  funcs: Record<string, FuncMeta>;
}

function FuncsPanel({ funcs }: FuncsPanelProps) {
  const [func, setFunc] = useAtom(funcAtom);

  return (
    <Accordion.Root
      type="single"
      collapsible
      className="flex w-full flex-col px-3 py-2 text-xs"
      value={func ?? undefined}
      onValueChange={(value) => setFunc(value)}
    >
      {Object.values(funcs).map((func) => (
        <Accordion.Item
          key={func.name}
          value={func.name}
          className="group flex flex-col"
        >
          <Accordion.Trigger className="flex w-full items-center gap-1 py-2 text-left font-mono uppercase">
            <svg
              className="transition-transform duration-200 group-data-[state=open]:rotate-90"
              width="8"
              height="8"
              viewBox="0 0 8 8"
              fill="currentColor"
            >
              <polygon points="0,0 8,4 0,8" />
            </svg>
            {func.name}
          </Accordion.Trigger>
          <Accordion.Content className="accordion-content ml-3 overflow-hidden">
            <div className="text-tiny bg-ps-border-tertiary border-ps-border-tertiary grid grid-cols-2 gap-y-px border">
              <span className="text-ps-text-secondary bg-ps-border-primary p-1 font-sans font-semibold">
                Minimum Coordinates
              </span>
              <span className="bg-ps-border-primary p-1 font-mono">
                ({func.min_coords.join(",")})
              </span>
              <span className="text-ps-text-secondary bg-ps-border-primary p-1 font-sans font-semibold">
                Maximum Coordinates
              </span>
              <span className="bg-ps-border-primary p-1 font-mono">
                ({func.max_coords.join(",")})
              </span>
              <span className="text-ps-text-secondary bg-ps-border-primary p-1 font-sans font-semibold">
                Minimum Value
              </span>
              <span className="bg-ps-border-primary p-1 font-mono">
                {func.min_value}
              </span>
              <span className="text-ps-text-secondary bg-ps-border-primary p-1 font-sans font-semibold">
                Maximum Value
              </span>
              <span className="bg-ps-border-primary p-1 font-mono">
                {func.max_value}
              </span>
              <span className="text-ps-text-secondary bg-ps-border-primary p-1 font-sans font-semibold">
                Maximum Store Count
              </span>
              <span className="bg-ps-border-primary p-1 font-mono">
                {func.max_store_count.toLocaleString()}
              </span>
              <span className="text-ps-text-secondary bg-ps-border-primary p-1 font-sans font-semibold">
                Maximum Load Count
              </span>
              <span className="bg-ps-border-primary p-1 font-mono">
                {func.max_load_count.toLocaleString()}
              </span>
            </div>
          </Accordion.Content>
        </Accordion.Item>
      ))}
    </Accordion.Root>
  );
}

export default FuncsPanel;
