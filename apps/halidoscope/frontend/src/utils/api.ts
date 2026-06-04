import { BACKEND_ENDPOINT } from "./constants";

import type { FuncStats } from "../types";

interface LoadTraceResponse {
  session_id: string;
  funcs: Record<string, FuncStats>;
  dag_edges: Record<string, string[]>;
  num_packets: number;
}

/**
 * Load a Halide trace from the specified path on disk.
 *
 * @param path The absolute path to the trace file on disk. Passed to the
 * --trace CLI argument. If a relative path is provided, the calling code is
 * responsible for resolving it against the current working directory.
 * @param signal An {@link AbortSignal} to cancel the request.
 * @returns An {@link LoadTraceResponse} containing the session ID, func stats,
 * DAG edges, and total number of packets in the trace.
 * @throws An error if the request fails or the response is not OK.
 */
export async function loadTracePath(
  path: string,
  signal: AbortSignal,
): Promise<LoadTraceResponse> {
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

/**
 * Deregister a trace session on the backend, freeing associated resources.
 *
 * @param session The session ID to deregister.
 * @returns A promise resolving to the JSON response from the backend.
 * @throws An error if the request fails or the response is not OK.
 */
export async function deregisterTrace(
  session: string,
): Promise<{ deleted: string }> {
  const response = await fetch(`${BACKEND_ENDPOINT}/session/${session}`, {
    method: "DELETE",
    headers: { "Content-Type": "application/json" },
  });

  if (!response.ok) {
    throw new Error(`Failed to deregister trace: ${response.statusText}`);
  }

  return response.json();
}
