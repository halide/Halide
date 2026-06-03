import { invoke } from "@tauri-apps/api/core";
import { getMatches } from "@tauri-apps/plugin-cli";
import { useEffect, useRef, useState } from "react";

import FuncCanvas from "./components/FuncCanvas";
import { Sidebar } from "./components/Sidebar";
import { FuncStats } from "./types";
import { BACKEND_ENDPOINT, WS_ENDPOINT } from "./utils/constants";

import "./App.css";

async function loadTracePath(path: string, signal: AbortSignal) {
  const response = await fetch(`${BACKEND_ENDPOINT}/load-path`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ path }),
    signal,
  });

  if (!response.ok) {
    throw new Error(`Failed to load trace: ${response.statusText}`);
  }

  return response.json();
}

async function deregisterTrace(session: string) {
  const response = await fetch(`${BACKEND_ENDPOINT}/session/${session}`, {
    method: "DELETE",
    headers: { "Content-Type": "application/json" },
  });

  if (!response.ok) {
    throw new Error(`Failed to deregister trace: ${response.statusText}`);
  }

  return response.json();
}

function App() {
  const [session, setSession] = useState<string>("");
  const [funcs, setFuncs] = useState<Record<string, FuncStats>>({});
  const wsRef = useRef<WebSocket | null>(null);

  useEffect(() => {
    const controller = new AbortController();

    async function loadTraceFromCLI() {
      const matches = await getMatches();
      const tracePath = matches.args.trace?.value;

      if (typeof tracePath === "string") {
        const resolved = tracePath.startsWith("/")
          ? tracePath
          : `${await invoke<string>("get_cwd")}/${tracePath}`;

        try {
          const { session_id, funcs } = await loadTracePath(
            resolved,
            controller.signal,
          );

          setSession(session_id);
          setFuncs(funcs);

          const ws = new WebSocket(`${WS_ENDPOINT}/ws/${session_id}`);
          wsRef.current = ws;
          ws.onopen = () =>
            console.log("WebSocket connected: session=%s", session_id);
          ws.onmessage = (event) =>
            console.log("WebSocket message:", JSON.parse(event.data));
          ws.onerror = (err) => console.error("WebSocket error:", err);
          ws.onclose = () => {
            wsRef.current = null;
          };
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
      wsRef.current?.close();
      if (session) {
        deregisterTrace(session)
          .then((res) => console.log("Successfully removed trace.", res.json()))
          .catch((err) => console.error(err));
      }
    };
  }, []);

  return (
    <main className="absolute inset-0 bg-slate-900 flex text-white">
      <Sidebar funcs={funcs} />
      <div className="flex-1 flex items-center justify-center overflow-x-auto gap-4 p-12">
        {Object.keys(funcs).length > 0 ? (
          Object.entries(funcs).map(([name, stats]) => (
            <FuncCanvas
              key={name}
              name={name}
              width={Math.max(
                1,
                (stats.max_coords[0] ?? 0) - (stats.min_coords[0] ?? 0),
              )}
              height={Math.max(
                1,
                (stats.max_coords[1] ?? stats.max_coords[0] ?? 0) -
                  (stats.min_coords[1] ?? stats.min_coords[0] ?? 0),
              )}
              xs={[]}
              ys={[]}
              values={[]}
            />
          ))
        ) : (
          <p className="animate-pulse">Loading trace...</p>
        )}
      </div>
    </main>
  );
}

export default App;
