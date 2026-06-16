import * as React from "react";

import { FuncStats } from "@/types";
import { CanvasRegistry } from "@/hooks/canvas-registry";

const TraceContext = React.createContext<{
  sessionId: string;
  funcs: Record<string, FuncStats>;
  dagEdges: Record<string, string[]>;
  packetCount: number;
  canvasRegistry: CanvasRegistry | null;
  globalMaxStoreCount: number;
  globalMaxLoadCount: number;
}>({
  sessionId: "",
  funcs: {},
  dagEdges: {},
  packetCount: 0,
  canvasRegistry: null,
  globalMaxStoreCount: 0,
  globalMaxLoadCount: 0,
});

export const TraceContextProvider = TraceContext.Provider;
export const useTraceContext = () => React.useContext(TraceContext);
