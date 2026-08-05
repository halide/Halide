import { Tabs } from "radix-ui";

import DebugPanel from "@/components/controls/DebugPanel";
import DisplayPanel from "@/components/controls/DisplayPanel";
import FuncsPanel from "@/components/controls/FuncsPanel";
import VisualizationPanel from "@/components/controls/VisualizationPanel";
import { FuncMeta } from "@/types/trace";

function ControlTabs({ funcs }: { funcs: Record<string, FuncMeta> }) {
  return (
    <div className="flex w-120">
      <div className="bg-ps-border-secondary h-full w-px" />
      <div className="bg-ps-border-primary h-full w-px" />
      <div className="flex flex-1 flex-col">
        <Tabs.Root
          defaultValue="funcs"
          className="bg-ps-primary text-ps-text-primary border-l-ps-border-primary flex flex-1 flex-col overflow-auto border-l text-xs"
        >
          <Tabs.List className="bg-ps-titlebar border-ps-border-primary flex border-y">
            <Tabs.Trigger
              value="funcs"
              className="data-[state=active]:bg-ps-primary data-[state=inactive]:bg-ps-titlebar data-[state=active]:text-ps-text-primary data-[state=inactive]:text-ps-text-secondary border-ps-border-primary border-r px-3 py-1 font-semibold"
            >
              Funcs
            </Tabs.Trigger>
            <Tabs.Trigger
              value="visualization"
              className="data-[state=active]:bg-ps-primary data-[state=inactive]:bg-ps-titlebar data-[state=active]:text-ps-text-primary data-[state=inactive]:text-ps-text-secondary border-ps-border-primary border-r px-3 py-1 font-semibold"
            >
              Visualization
            </Tabs.Trigger>
            <Tabs.Trigger
              value="debug"
              className="data-[state=active]:bg-ps-primary data-[state=inactive]:bg-ps-titlebar data-[state=active]:text-ps-text-primary data-[state=inactive]:text-ps-text-secondary border-ps-border-primary border-r px-3 py-1 font-semibold"
            >
              Debug
            </Tabs.Trigger>
          </Tabs.List>
          <Tabs.Content value="funcs" className="flex-1 overflow-auto">
            <FuncsPanel funcs={funcs} />
          </Tabs.Content>
          <Tabs.Content value="visualization" className="flex-1 overflow-auto">
            <VisualizationPanel />
          </Tabs.Content>
          <Tabs.Content value="debug" className="flex-1">
            <DebugPanel />
          </Tabs.Content>
        </Tabs.Root>
        <Tabs.Root
          defaultValue="display"
          className="bg-ps-primary text-ps-text-primary border-l-ps-border-primary flex flex-col overflow-auto border-l text-xs"
        >
          <Tabs.List className="bg-ps-titlebar border-ps-border-primary flex border-y">
            <Tabs.Trigger
              value="display"
              className="data-[state=active]:bg-ps-primary data-[state=inactive]:bg-ps-titlebar data-[state=active]:text-ps-text-primary data-[state=inactive]:text-ps-text-secondary border-ps-border-primary border-r px-3 py-1 font-semibold"
            >
              Display
            </Tabs.Trigger>
          </Tabs.List>
          <Tabs.Content value="display">
            <DisplayPanel />
          </Tabs.Content>
        </Tabs.Root>
      </div>
    </div>
  );
}

export default ControlTabs;
