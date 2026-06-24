import { invoke } from "@tauri-apps/api/core";
import { getMatches } from "@tauri-apps/plugin-cli";
import { useSetAtom } from "jotai";
import * as React from "react";

import Tracer from "@/components/views/tracer/Tracer";
import { TraceContextProvider } from "@/hooks/trace";
import type { FuncMeta } from "@/types";
import { openTrace } from "@/utils/api";

import "./App.css";
import { funcAtom } from "./state/func";

function App() {
  const [funcs, setFuncs] = React.useState<Record<string, FuncMeta>>({});
  const [dagEdges, setDagEdges] = React.useState<Record<string, string[]>>({});
  const [packetCount, setPacketCount] = React.useState<number>(0);
  const [globalMaxStoreCount, setGlobalMaxStoreCount] =
    React.useState<number>(0);
  const [globalMaxLoadCount, setGlobalMaxLoadCount] = React.useState<number>(0);
  const [globalMaxRedundantCount, setGlobalMaxRedundantCount] =
    React.useState<number>(0);
  const [globalMaxReuseDistance, setGlobalMaxReuseDistance] =
    React.useState<number>(0);

  const setActiveFunc = useSetAtom(funcAtom);

  React.useEffect(() => {
    async function loadTraceFromCLI() {
      const matches = await getMatches();
      const tracePath = matches.args.trace?.value;

      if (typeof tracePath !== "string") {
        return;
      }

      const resolved = tracePath.startsWith("/")
        ? tracePath
        : `${await invoke<string>("get_cwd")}/${tracePath}`;

      try {
        const {
          funcs,
          total_packets,
          dag_edges,
          global_max_store_count,
          global_max_load_count,
          global_max_redundant_count,
          global_max_reuse_distance,
        } = await openTrace(resolved);

        const byName: Record<string, FuncMeta> = {};
        for (const func of funcs) {
          byName[func.name] = func;
        }

        setFuncs(byName);
        setDagEdges(dag_edges);
        setPacketCount(total_packets);
        setGlobalMaxStoreCount(global_max_store_count);
        setGlobalMaxLoadCount(global_max_load_count);
        setGlobalMaxRedundantCount(global_max_redundant_count);
        setGlobalMaxReuseDistance(global_max_reuse_distance);
        setActiveFunc(funcs[0]?.name ?? "");
      } catch (err) {
        console.error("Error loading trace from CLI: ", err);
      }
    }

    loadTraceFromCLI();
  }, [setActiveFunc]);

  return (
    <TraceContextProvider
      value={{
        funcs,
        dagEdges,
        packetCount,
        globalMaxStoreCount,
        globalMaxLoadCount,
        globalMaxRedundantCount,
        globalMaxReuseDistance,
      }}
    >
      <main className="h-screen w-screen">
        <Tracer />
      </main>
    </TraceContextProvider>
  );
}

export default App;
