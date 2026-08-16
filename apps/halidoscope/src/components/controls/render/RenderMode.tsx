import { useAtom } from "jotai";

import Select from "@/components/shared/Select";
import { renderAtom, RENDER_MODES, type RenderMode } from "@/state/render";

function RenderMode() {
  const [render, setRender] = useAtom(renderAtom);

  return (
    <Select
      value={render.renderMode}
      onValueChange={(value) =>
        setRender({ ...render, renderMode: value as RenderMode })
      }
      items={RENDER_MODES.map((mode) => ({ value: mode, label: mode }))}
    />
  );
}

export default RenderMode;
