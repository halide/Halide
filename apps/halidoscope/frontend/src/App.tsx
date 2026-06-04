import { invoke } from "@tauri-apps/api/core";
import { getMatches } from "@tauri-apps/plugin-cli";
import { ReactFlowProvider } from "@xyflow/react";
import * as React from "react";

import Canvas from "./components/Canvas";
import Sidebar from "./components/Sidebar";
import Timeline from "./components/Timeline";
import {
  CanvasRegistry,
  CanvasRegistryProvider,
} from "./hooks/canvas-registry";
import { FuncStats } from "./types";
import { loadTracePath, deregisterTrace } from "./utils/api";

import "./App.css";

function App() {
  const [sessionId, setSessionId] = React.useState<string>("");
  const [funcs, setFuncs] = React.useState<Record<string, FuncStats>>({});
  const [dagEdges, setDagEdges] = React.useState<Record<string, string[]>>({});
  const [packetCount, setPacketCount] = React.useState<number>(0);
  const [canvasRegistry, setCanvasRegistry] =
    React.useState<CanvasRegistry | null>(null);

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
          const { session_id, funcs, dag_edges, num_packets } =
            await loadTracePath(resolved, controller.signal);

          loadedSessionId = session_id;
          setSessionId(session_id);
          setFuncs(funcs);
          setDagEdges(dag_edges);
          setPacketCount(num_packets);

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
    <CanvasRegistryProvider value={canvasRegistry}>
      <main className="absolute inset-0 bg-ps-secondary flex text-white">
        <Sidebar funcs={funcs} />
        <div className="flex flex-col flex-1 min-h-0">
          <div className="flex-1 min-h-0 relative">
            {Object.keys(funcs).length > 0 ? (
              <ReactFlowProvider>
                <Canvas funcs={funcs} dagEdges={dagEdges} />
              </ReactFlowProvider>
            ) : (
              <div className="absolute inset-0 flex items-center justify-center">
                <p className="animate-pulse">Loading trace...</p>
              </div>
            )}
          </div>
          <Timeline
            packetCount={packetCount}
            sessionId={sessionId}
            canvasRegistry={canvasRegistry}
          />
        </div>
      </main>
    </CanvasRegistryProvider>
  );
}

export default App;
