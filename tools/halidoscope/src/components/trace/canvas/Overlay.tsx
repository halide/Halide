import { clsx } from "clsx";
import type * as React from "react";

function Overlay({
  children,
  className,
}: React.PropsWithChildren<{ className: string }>) {
  return (
    <div
      className={clsx(
        "bg-ps-primary text-ps-text-primary absolute rounded px-2 py-1 text-xs",
        className,
      )}
    >
      {children}
    </div>
  );
}

export default Overlay;
