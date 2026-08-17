import { Separator } from "radix-ui";

import ControlSection from "@/components/controls/ControlSection";
import GraphDisplay from "@/components/controls/graph/GraphDisplay";
import PlaybackRate from "@/components/controls/playback/PlaybackRate";

function DisplayPanel() {
  return (
    <div className="flex flex-col gap-4 px-3 py-4">
      <ControlSection title="Graph Display">
        <GraphDisplay />
      </ControlSection>
      <Separator.Root className="bg-ps-border-tertiary h-px" />
      <ControlSection title="Playback">
        <PlaybackRate />
      </ControlSection>
    </div>
  );
}

export default DisplayPanel;
