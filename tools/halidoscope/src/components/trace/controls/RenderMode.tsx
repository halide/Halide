import Select from "@/components/shared/Select";
import { useFuncView } from "@/hooks/funcView";
import { renderModesFor, type RenderMode } from "@/state/render";
import type { FuncMeta } from "@/types/trace";

interface Props {
  func: FuncMeta;
}

function RenderMode({ func }: Props) {
  const [view, update] = useFuncView(func);

  return (
    <Select
      value={view.renderMode}
      onValueChange={(value) => update({ renderMode: value as RenderMode })}
      items={renderModesFor(func).map((mode) => ({ value: mode, label: mode }))}
    />
  );
}

export default RenderMode;
