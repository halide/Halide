import { useAtom } from "jotai";
import * as React from "react";

import { funcViewAtom, getFuncView, type FuncView } from "@/state/funcView";
import type { FuncMeta } from "@/types/trace";

/** The view for a Func, and a function to modify it. */
export function useFuncView(
  func: FuncMeta,
): [FuncView, (change: Partial<FuncView>) => void] {
  const [views, setViews] = useAtom(funcViewAtom);
  const update = React.useCallback(
    (change: Partial<FuncView>) => {
      setViews((prev) => ({
        ...prev,
        [func.name]: { ...getFuncView(prev[func.name], func), ...change },
      }));
    },
    [func, setViews],
  );
  return [getFuncView(views[func.name], func), update];
}
