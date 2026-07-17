import { Select } from "radix-ui";
import { useAtom } from "jotai";

import ArrowDownIcon from "@/components/icons/ArrowDownIcon";
import { renderAtom, RENDER_MODES, type RenderMode } from "@/state/render";

function VisualizationSelect() {
  const [render, setRender] = useAtom(renderAtom);

  return (
    <Select.Root
      value={render.renderMode}
      onValueChange={(value) =>
        setRender({ ...render, renderMode: value as RenderMode })
      }
    >
      <Select.Trigger
        id="visualization-select"
        className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary inline-flex h-8 w-full items-center justify-center rounded border px-2 focus:outline-none"
      >
        <Select.Value className="text-ps-text-primary" />
        <Select.Icon className="ml-auto">
          <ArrowDownIcon />
        </Select.Icon>
      </Select.Trigger>
      <Select.Content
        position="popper"
        sideOffset={4}
        className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary z-10 max-h-(--radix-select-content-available-height) w-(--radix-select-trigger-width) rounded border"
      >
        <Select.Viewport>
          {RENDER_MODES.map((value) => (
            <Select.Item
              key={value}
              value={value}
              className="hover:bg-ps-border-tertiary cursor-pointer p-2 transition-colors"
            >
              <Select.ItemText>{value}</Select.ItemText>
            </Select.Item>
          ))}
        </Select.Viewport>
      </Select.Content>
    </Select.Root>
  );
}

export default VisualizationSelect;
