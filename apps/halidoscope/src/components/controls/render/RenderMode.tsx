import { Select } from "radix-ui";
import { useAtom } from "jotai";

import { renderModeAtom, RENDER_MODES, type RenderMode } from "@/state/render";

function VisualizationSelect() {
  const [renderMode, setVisualizationMode] = useAtom(renderModeAtom);

  return (
    <Select.Root
      value={renderMode}
      onValueChange={(value) => setVisualizationMode(value as RenderMode)}
    >
      <Select.Trigger
        id="visualization-select"
        className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary inline-flex h-8 w-full items-center justify-center rounded border px-2 focus:outline-none"
      >
        <Select.Value className="text-ps-text-primary" />
        <Select.Icon className="ml-auto">
          <svg
            xmlns="http://www.w3.org/2000/svg"
            fill="none"
            viewBox="0 0 24 24"
            strokeWidth={1.5}
            stroke="currentColor"
            className="h-3 w-3"
          >
            <path
              strokeLinecap="round"
              strokeLinejoin="round"
              d="M19.5 8.25l-7.5 7.5-7.5-7.5"
            />
          </svg>
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
