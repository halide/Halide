import { useAtom } from "jotai";

import Checkbox from "@/components/shared/Checkbox";
import { livenessAtom } from "@/state/liveness";

function LivenessControls() {
  const [liveness, setLiveness] = useAtom(livenessAtom);

  return (
    <Checkbox
      checked={liveness.active}
      id="highlight-liveness-checkbox"
      label="Highlight Liveness"
      onCheckedChange={(active) => setLiveness({ active })}
    />
  );
}

export default LivenessControls;
