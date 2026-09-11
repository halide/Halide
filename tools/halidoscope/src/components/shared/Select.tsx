import { Label, Select as RadixSelect } from "radix-ui";

interface Item {
  value: string;
  label: string;
}

interface Props {
  id?: string;
  label?: string;
  value: string;
  onValueChange: (value: string) => void;
  items: Item[];
}

function BaseSelect({
  id,
  items,
  value,
  onValueChange,
}: Exclude<Props, "label">) {
  return (
    <RadixSelect.Root value={value} onValueChange={onValueChange}>
      <RadixSelect.Trigger
        id={id}
        className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary inline-flex h-8 w-full items-center justify-center rounded border px-2 focus:outline-none"
      >
        <span className="truncate">
          <RadixSelect.Value />
        </span>
        <RadixSelect.Icon className="ml-auto">
          <svg
            xmlns="http://www.w3.org/2000/svg"
            fill="none"
            viewBox="0 0 24 24"
            strokeWidth="1.5"
            stroke="currentColor"
            className="h-3 w-3"
          >
            <path
              strokeLinecap="round"
              strokeLinejoin="round"
              d="M19.5 8.25l-7.5 7.5-7.5-7.5"
            />
          </svg>
        </RadixSelect.Icon>
      </RadixSelect.Trigger>
      <RadixSelect.Content
        position="popper"
        sideOffset={4}
        className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary z-10 max-h-(--radix-select-content-available-height) w-(--radix-select-trigger-width) rounded border"
      >
        <RadixSelect.Viewport>
          {items.map((item) => (
            <RadixSelect.Item
              key={item.value}
              value={item.value}
              className="hover:bg-ps-border-tertiary cursor-pointer p-2 transition-colors"
            >
              <RadixSelect.ItemText className="truncate">
                {item.label}
              </RadixSelect.ItemText>
            </RadixSelect.Item>
          ))}
        </RadixSelect.Viewport>
      </RadixSelect.Content>
    </RadixSelect.Root>
  );
}

function Select({ id, label, value, onValueChange, items }: Props) {
  if (!id || !label) {
    return (
      <BaseSelect
        id={id}
        items={items}
        onValueChange={onValueChange}
        value={value}
      />
    );
  }

  return (
    <div className="flex flex-col gap-1">
      <Label.Root className="text-ps-text-primary/60" htmlFor={id}>
        {label}
      </Label.Root>
      <BaseSelect
        id={id}
        items={items}
        onValueChange={onValueChange}
        value={value}
      />
    </div>
  );
}

export default Select;
