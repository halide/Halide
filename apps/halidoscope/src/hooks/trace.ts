import * as React from "react";

import { FuncMeta } from "@/types";

const TraceContext = React.createContext<{
  funcs: Record<string, FuncMeta>;
  dagEdges: Record<string, string[]>;
  packetCount: number;
  globalMaxReuseDistance: number;
}>({
  funcs: {},
  dagEdges: {},
  packetCount: 0,
  globalMaxReuseDistance: 0,
});

export const TraceContextProvider = TraceContext.Provider;
export const useTraceContext = () => React.useContext(TraceContext);
