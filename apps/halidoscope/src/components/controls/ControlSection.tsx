import { Label } from "radix-ui";
import type * as React from "react";

interface ControlSectionProps {
  title: string;
}

function ControlSection({
  title,
  children,
}: React.PropsWithChildren<ControlSectionProps>) {
  return (
    <div className="flex flex-col gap-2">
      <Label.Root className="text-ps-text-primary font-semibold tracking-widest uppercase">
        {title}
      </Label.Root>
      <div className="flex flex-col gap-2">{children}</div>
    </div>
  );
}

export default ControlSection;
