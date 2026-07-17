import { Separator } from "radix-ui";

import ControlSection from "@/components/controls/ControlSection";
import InfControls from "@/components/controls/inf/InfControls";
import NaNControls from "@/components/controls/nan/NaNControls";

function DebugPanel() {
  return (
    <div className="flex flex-col gap-4 px-3 py-4">
      <ControlSection title="NaN Highlighting">
        <NaNControls />
      </ControlSection>
      <Separator.Root className="bg-ps-border-tertiary h-px" />
      <ControlSection title="Inf Highlighting">
        <InfControls />
      </ControlSection>
    </div>
  );
}

export default DebugPanel;
