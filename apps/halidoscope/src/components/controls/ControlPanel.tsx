import { Checkbox } from "radix-ui";

interface ControlPanelProps {
  setHidden: React.Dispatch<React.SetStateAction<boolean>>;
}

function ControlPanel({ setHidden }: ControlPanelProps) {
  return (
    <div className="bg-ps-primary absolute top-2 right-2 rounded p-2 text-xs">
      <div className="flex items-center gap-2">
        <Checkbox.Root
          className="bg-ps-border-primary flex h-4 w-4 items-center justify-center rounded-xs"
          defaultChecked
          id="toggle-edges"
          onCheckedChange={(checked) => setHidden(checked === false)}
        >
          <Checkbox.Indicator className="CheckboxIndicator">
            <svg
              fill="none"
              stroke="currentColor"
              strokeLinecap="round"
              strokeLinejoin="round"
              strokeWidth="1.5"
              shapeRendering="geometricPrecision"
              viewBox="0 0 24 24"
              height="100%"
              width="100%"
            >
              <path d="M20 6L9 17l-5-5"></path>
            </svg>
          </Checkbox.Indicator>
        </Checkbox.Root>
        <label htmlFor="toggle-edges">Edges</label>
      </div>
    </div>
  );
}

export default ControlPanel;
