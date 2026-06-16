import { invoke } from "@tauri-apps/api/core";
import { getMatches } from "@tauri-apps/plugin-cli";
import * as React from "react";

import ViewTabs from "@/components/views/ViewTabs";
import type { FuncStats } from "@/types";
import { CanvasRegistry } from "@/hooks/canvas-registry";
import { TraceContextProvider } from "@/hooks/trace";
import { loadTracePath, deregisterTrace } from "@/utils/api";

import "./App.css";

function App() {
  const [sessionId, setSessionId] = React.useState<string>("");
  const [funcs, setFuncs] = React.useState<Record<string, FuncStats>>({});
  const [dagEdges, setDagEdges] = React.useState<Record<string, string[]>>({});
  const [packetCount, setPacketCount] = React.useState<number>(0);
  const [canvasRegistry, setCanvasRegistry] =
    React.useState<CanvasRegistry | null>(null);
  const [globalMaxStoreCount, setGlobalMaxStoreCount] =
    React.useState<number>(0);
  const [globalMaxLoadCount, setGlobalMaxLoadCount] = React.useState<number>(0);

  React.useEffect(() => {
    const controller = new AbortController();
    // Track the loaded session ID locally so the cleanup closure always has
    // the correct value even though sessionId state starts as "".
    let loadedSessionId = "";

    async function loadTraceFromCLI() {
      const matches = await getMatches();
      const tracePath = matches.args.trace?.value;

      if (typeof tracePath === "string") {
        const resolved = tracePath.startsWith("/")
          ? tracePath
          : `${await invoke<string>("get_cwd")}/${tracePath}`;

        try {
          const {
            session_id,
            funcs,
            dag_edges,
            num_packets,
            global_max_store_count,
            global_max_load_count,
          } = await loadTracePath(resolved, controller.signal);

          loadedSessionId = session_id;
          setSessionId(session_id);
          setFuncs(funcs);
          setDagEdges(dag_edges);
          setPacketCount(num_packets);
          setGlobalMaxStoreCount(global_max_store_count);
          setGlobalMaxLoadCount(global_max_load_count);
          setCanvasRegistry(new CanvasRegistry());
        } catch (err) {
          if ((err as Error).name !== "AbortError") {
            console.error("Error loading trace from CLI: ", err);
          }
        }
      }
    }

    loadTraceFromCLI();

    return () => {
      controller.abort();

      if (loadedSessionId) {
        deregisterTrace(loadedSessionId).catch((err) => console.error(err));
      }
    };
  }, []);

  return (
    <TraceContextProvider
      value={{
        sessionId,
        funcs,
        dagEdges,
        packetCount,
        canvasRegistry,
        globalMaxStoreCount,
        globalMaxLoadCount,
      }}
    >
      <main className="absolute inset-0 flex">
        <ViewTabs />
      </main>
    </TraceContextProvider>
  );
}

export default App;
