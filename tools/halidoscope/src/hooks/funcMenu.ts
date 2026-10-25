import { useSetAtom } from "jotai";
import * as React from "react";

import { funcMenuAtom } from "@/state/navigation";

interface MenuEvent {
  clientX: number;
  clientY: number;
  preventDefault: () => void;
}

/** Returns a handler that opens the Func navigation menu at the event. */
export function useOpenFuncMenu() {
  const setMenu = useSetAtom(funcMenuAtom);
  return React.useCallback(
    (event: MenuEvent, name: string) => {
      event.preventDefault();
      setMenu({ name, x: event.clientX, y: event.clientY });
    },
    [setMenu],
  );
}
