import { Tabs } from "radix-ui";

import Tracer from "@/components/views/tracer/Tracer";

function ViewTabs() {
  return (
    <Tabs.Root
      defaultValue="tracer"
      className="w-full h-full flex flex-col text-sm bg-ps-primary text-ps-text-primary"
    >
      <Tabs.List className="flex bg-ps-titlebar border-y border-ps-border-primary">
        <Tabs.Trigger
          value="tracer"
          className="px-3 py-1 data-[state=active]:bg-ps-primary data-[state=inactive]:bg-ps-titlebar data-[state=active]:text-ps-text-primary data-[state=inactive]:text-ps-text-secondary font-semibold border-r border-ps-border-primary"
        >
          Tracer
        </Tabs.Trigger>
      </Tabs.List>
      <Tabs.Content value="tracer" className="flex-1">
        <Tracer />
      </Tabs.Content>
    </Tabs.Root>
  );
}

export default ViewTabs;
