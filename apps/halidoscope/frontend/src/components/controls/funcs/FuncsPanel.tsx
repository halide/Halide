import { useAtom } from "jotai";
import { Accordion } from "radix-ui";

import type { FuncStats } from "@/types";
import { funcAtom } from "@/state/func";

interface FuncsPanelProps {
  funcs: Record<string, FuncStats>;
}

function FuncsPanel({ funcs }: FuncsPanelProps) {
  const [func, setFunc] = useAtom(funcAtom);

  return (
    <Accordion.Root
      type="single"
      collapsible
      className="w-full text-xs px-3 py-2 flex flex-col"
      value={func ?? undefined}
      onValueChange={(value) => setFunc(value)}
    >
      {Object.values(funcs).map((func) => (
        <Accordion.Item
          key={func.name}
          value={func.name}
          className="group flex flex-col"
        >
          <Accordion.Trigger className="flex items-center gap-1 w-full text-left uppercase font-mono py-2">
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
          <Accordion.Content className="accordion-content overflow-hidden ml-3">
            <div className="grid grid-cols-2 text-tiny bg-ps-border-tertiary gap-y-px border border-ps-border-tertiary">
              <span className="font-sans text-ps-text-secondary font-semibold bg-ps-primary p-1">
                Minimum Coordinates
              </span>
              <span className="font-mono bg-ps-primary p-1">
                ({func.min_coords.join(",")})
              </span>
              <span className="font-sans text-ps-text-secondary font-semibold bg-ps-primary p-1">
                Maximum Coordinates
              </span>
              <span className="font-mono bg-ps-primary p-1">
                ({func.max_coords.join(",")})
              </span>
              <span className="font-sans text-ps-text-secondary font-semibold bg-ps-primary p-1">
                Minimum Value
              </span>
              <span className="font-mono bg-ps-primary p-1">
                {func.min_value}
              </span>
              <span className="font-sans text-ps-text-secondary font-semibold bg-ps-primary p-1">
                Maximum Value
              </span>
              <span className="font-mono bg-ps-primary p-1">
                {func.max_value}
              </span>
              <span className="font-sans text-ps-text-secondary font-semibold bg-ps-primary p-1">
                Maximum Store Count
              </span>
              <span className="font-mono bg-ps-primary p-1">
                {func.max_store_count}
              </span>
              <span className="font-sans text-ps-text-secondary font-semibold bg-ps-primary p-1">
                Maximum Load Count
              </span>
              <span className="font-mono bg-ps-primary p-1">
                {func.max_load_count}
              </span>
            </div>
          </Accordion.Content>
        </Accordion.Item>
      ))}
    </Accordion.Root>
  );
}

export default FuncsPanel;
