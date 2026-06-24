import * as React from "react";

import { FuncMeta } from "@/types";

const TraceContext = React.createContext<{
  funcs: Record<string, FuncMeta>;
  dagEdges: Record<string, string[]>;
  packetCount: number;
  globalMaxStoreCount: number;
  globalMaxLoadCount: number;
  globalMaxRedundantCount: number;
  globalMaxReuseDistance: number;
}>({
  funcs: {},
  dagEdges: {},
  packetCount: 0,
  globalMaxStoreCount: 0,
  globalMaxLoadCount: 0,
  globalMaxRedundantCount: 0,
  globalMaxReuseDistance: 0,
});

export const TraceContextProvider = TraceContext.Provider;
export const useTraceContext = () => React.useContext(TraceContext);
