import { Separator } from "radix-ui";

import GraphDisplay from "@/components/trace/controls/GraphDisplay";
import PlaybackRate from "@/components/trace/controls/PlaybackRate";
import PanelSection from "@/components/trace/panels/PanelSection";

function DisplayPanel() {
  return (
    <div className="flex flex-col gap-4 px-3 py-4">
      <PanelSection title="Graph Display">
        <GraphDisplay />
      </PanelSection>
      <Separator.Root className="bg-ps-border-tertiary h-px" />
      <PanelSection title="Playback">
        <PlaybackRate />
      </PanelSection>
    </div>
  );
}

export default DisplayPanel;
