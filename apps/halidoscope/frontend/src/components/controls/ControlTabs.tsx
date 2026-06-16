import { Tabs } from "radix-ui";

import FuncsPanel from "@/components/controls/funcs/FuncsPanel";
import PlaybackPanel from "@/components/controls/playback/PlaybackPanel";
import { FuncStats } from "@/types";

function ControlTabs({ funcs }: { funcs: Record<string, FuncStats> }) {
  return (
    <div className="flex h-full min-w-80">
      <div className="w-px h-full bg-ps-border-secondary" />
      <div className="w-px h-full bg-ps-border-primary" />
      <Tabs.Root
        defaultValue="funcs"
        className="flex flex-col flex-1 text-xs bg-ps-primary text-ps-text-primary border-l border-l-ps-border-primary"
      >
        <Tabs.List className="flex bg-ps-titlebar border-y border-ps-border-primary">
          <Tabs.Trigger
            value="funcs"
            className="px-3 py-1 data-[state=active]:bg-ps-primary data-[state=inactive]:bg-ps-titlebar data-[state=active]:text-ps-text-primary data-[state=inactive]:text-ps-text-secondary font-semibold border-r border-ps-border-primary"
          >
            Funcs
          </Tabs.Trigger>
          <Tabs.Trigger
            value="playback"
            className="px-3 py-1 data-[state=active]:bg-ps-primary data-[state=inactive]:bg-ps-titlebar data-[state=active]:text-ps-text-primary data-[state=inactive]:text-ps-text-secondary font-semibold border-r border-ps-border-primary"
          >
            Playback
          </Tabs.Trigger>
        </Tabs.List>
        <Tabs.Content value="funcs" className="flex-1 overflow-auto">
          <FuncsPanel funcs={funcs} />
        </Tabs.Content>
        <Tabs.Content value="playback" className="flex-1">
          <PlaybackPanel />
        </Tabs.Content>
      </Tabs.Root>
    </div>
  );
}

export default ControlTabs;
