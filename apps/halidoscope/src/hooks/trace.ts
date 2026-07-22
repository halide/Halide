import * as React from "react";

import { FuncMeta, StatsMeta } from "@/types";

const TraceContext = React.createContext<{
  funcs: Record<string, FuncMeta>;
  dagEdges: Record<string, string[]>;
  packetCount: number;
  stats: StatsMeta;
}>({
  funcs: {},
  dagEdges: {},
  packetCount: 0,
  stats: {
    global_max_store_count: 0,
    global_max_load_count: 0,
    global_max_redundant_store_count: 0,
    global_max_reuse_distance: 0,
  },
});

export const TraceContextProvider = TraceContext.Provider;
export const useTraceContext = () => React.useContext(TraceContext);
