/** A packet-index interval `[start, end]`. Mirrors the Rust `IndexRange`. */
export interface IndexRange {
  start: number;
  end: number;
}

/**
 * Per-Func metadata returned by the `open_trace` command. Mirrors the Rust
 * `FuncMeta`. Carries everything the UI needs to size canvases, bound the
 * scrub timeline, and populate the inspector panel.
 */
export interface FuncMeta extends Record<string, unknown> {
  name: string;
  width: number;
  height: number;
  channels: number;
  num_stores: number;
  min_coords: number[];
  max_coords: number[];
  min_value: number | null;
  max_value: number | null;
  max_store_count: number;
  max_load_count: number;
  max_redundant_store_count: number;
  max_reuse_distance: number;
  buffer_liveness: IndexRange;
  produce_ranges: IndexRange[];
  consume_ranges: IndexRange[];
  thread_count: number;
}

/** Top-level payload returned by `open_trace`. Mirrors the Rust `TraceMeta`. */
export interface TraceMeta {
  funcs: FuncMeta[];
  total_packets: number;
  dag_edges: Record<string, string[]>;
  global_max_reuse_distance: number;
}

export type NodeTypes = "funcNode";
export type EdgeTypes = "funcEdge";

export type AnimationMode = "Blink" | "Pulse" | "None";
