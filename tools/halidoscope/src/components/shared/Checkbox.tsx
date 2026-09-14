import { Checkbox as RadixCheckbox } from "radix-ui";

interface Props {
  checked: boolean;
  id: string;
  label: string;
  onCheckedChange: (checked: boolean) => void;
}

function Checkbox({ checked, id, label, onCheckedChange }: Props) {
  return (
    <div className="flex items-center gap-2">
      <RadixCheckbox.Root
        className="bg-ps-border-primary border-ps-border-tertiary flex h-4 w-4 items-center justify-center rounded-xs border"
        checked={checked}
        id={id}
        onCheckedChange={(checked) => onCheckedChange(!!checked)}
      >
        <RadixCheckbox.Indicator>
          <svg
            xmlns="http://www.w3.org/2000/svg"
            width="12"
            height="12"
            viewBox="0 0 12 12"
            fill="none"
          >
            <path
              d="M1.5 6L4.5 9L10.5 3"
              stroke="currentColor"
              strokeWidth="1.5"
              strokeLinecap="round"
              strokeLinejoin="round"
            />
          </svg>
        </RadixCheckbox.Indicator>
      </RadixCheckbox.Root>
      <label className="text-ps-text-primary/60" htmlFor={id}>
        {label}
      </label>
    </div>
  );
}

export default Checkbox;
