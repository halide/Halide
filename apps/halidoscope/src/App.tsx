import { invoke } from "@tauri-apps/api/core";
import { getMatches } from "@tauri-apps/plugin-cli";
import { useSetAtom } from "jotai";
import * as React from "react";

import Tracer from "@/components/views/tracer/Tracer";
import { TraceContextProvider } from "@/hooks/trace";
import { funcAtom } from "@/state/func";
import type { FuncMeta, StatsMeta } from "@/types";
import { openTrace } from "@/utils/api";

import "./App.css";

function App() {
  const [funcs, setFuncs] = React.useState<Record<string, FuncMeta>>({});
  const [dagEdges, setDagEdges] = React.useState<Record<string, string[]>>({});
  const [packetCount, setPacketCount] = React.useState<number>(0);
  const [stats, setStats] = React.useState<StatsMeta>({
    global_max_store_count: 0,
    global_max_load_count: 0,
    global_max_redundant_store_count: 0,
    global_max_reuse_distance: 0,
    global_thread_ids: [],
  });

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
        const { funcs, total_packets, dag_edges, stats } =
          await openTrace(resolved);

        const byName: Record<string, FuncMeta> = {};
        for (const func of funcs) {
          byName[func.name] = func;
        }

        setFuncs(byName);
        setDagEdges(dag_edges);
        setPacketCount(total_packets);
        setStats(stats);
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
        stats,
      }}
    >
      <main className="h-screen w-screen">
        <Tracer />
      </main>
    </TraceContextProvider>
  );
}

export default App;
