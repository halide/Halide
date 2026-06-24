import { Tabs } from "radix-ui";

import FuncsPanel from "@/components/controls/funcs/FuncsPanel";
import VisualizationsPanel from "@/components/controls/visualizations/VisualizationsPanel";
import { FuncMeta } from "@/types";

function ControlTabs({ funcs }: { funcs: Record<string, FuncMeta> }) {
  return (
    <div className="flex h-full min-w-80">
      <div className="bg-ps-border-secondary h-full w-px" />
      <div className="bg-ps-border-primary h-full w-px" />
      <Tabs.Root
        defaultValue="funcs"
        className="bg-ps-primary text-ps-text-primary border-l-ps-border-primary flex flex-1 flex-col border-l text-xs"
      >
        <Tabs.List className="bg-ps-titlebar border-ps-border-primary flex border-y">
          <Tabs.Trigger
            value="funcs"
            className="data-[state=active]:bg-ps-primary data-[state=inactive]:bg-ps-titlebar data-[state=active]:text-ps-text-primary data-[state=inactive]:text-ps-text-secondary border-ps-border-primary border-r px-3 py-1 font-semibold"
          >
            Funcs
          </Tabs.Trigger>
          <Tabs.Trigger
            value="visualizations"
            className="data-[state=active]:bg-ps-primary data-[state=inactive]:bg-ps-titlebar data-[state=active]:text-ps-text-primary data-[state=inactive]:text-ps-text-secondary border-ps-border-primary border-r px-3 py-1 font-semibold"
          >
            Visualizations
          </Tabs.Trigger>
        </Tabs.List>
        <Tabs.Content value="funcs" className="flex-1 overflow-auto">
          <FuncsPanel funcs={funcs} />
        </Tabs.Content>
        <Tabs.Content value="visualizations" className="flex-1">
          <VisualizationsPanel />
        </Tabs.Content>
      </Tabs.Root>
    </div>
  );
}

export default ControlTabs;
