import { Label } from "radix-ui";
import type * as React from "react";

interface Props {
  title: string;
}

function PanelSection({ title, children }: React.PropsWithChildren<Props>) {
  return (
    <div className="flex flex-col gap-2">
      <Label.Root className="text-ps-text-primary font-semibold tracking-widest uppercase">
        {title}
      </Label.Root>
      <div className="flex flex-col gap-2">{children}</div>
    </div>
  );
}

export default PanelSection;
