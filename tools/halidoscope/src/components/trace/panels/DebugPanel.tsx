import { Separator } from "radix-ui";

import PanelSection from "@/components/trace/panels/PanelSection";
import InfControls from "@/components/trace/controls/InfControls";
import LivenessControls from "@/components/trace/controls/LivenessControls";
import NaNControls from "@/components/trace/controls/NaNControls";

function DebugPanel() {
  return (
    <div className="flex flex-col gap-4 px-3 py-4">
      <PanelSection title="Liveness Highlighting">
        <LivenessControls />
      </PanelSection>
      <Separator.Root className="bg-ps-border-tertiary h-px" />
      <PanelSection title="NaN Highlighting">
        <NaNControls />
      </PanelSection>
      <Separator.Root className="bg-ps-border-tertiary h-px" />
      <PanelSection title="Inf Highlighting">
        <InfControls />
      </PanelSection>
      <Separator.Root className="bg-ps-border-tertiary h-px" />
    </div>
  );
}

export default DebugPanel;
