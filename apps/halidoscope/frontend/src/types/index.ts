/** How a Func's values are mapped to pixels. Mirrors the Rust `RenderMode`. */
export type RenderMode = "grayscale" | "rgb";

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
  default_mode: RenderMode;
  num_stores: number;
  min_coords: number[];
  max_coords: number[];
  min_value: number | null;
  max_value: number | null;
  max_store_count: number;
  max_load_count: number;
  max_redundant_count: number;
  /**
   * Frequency distributions of per-pixel counts, indexed by count value (index `k` holds the
   * number of pixel locations with exactly `k` stores/loads/redundant stores). The `0` bin is
   * included. Length is the corresponding `max_*_count + 1`; empty when the Func has no usable
   * extent. Ready to render directly as a histogram.
   */
  store_count_histogram: number[];
  load_count_histogram: number[];
  redundant_count_histogram: number[];
}

/** Top-level payload returned by `open_trace`. Mirrors the Rust `TraceMeta`. */
export interface TraceMeta {
  funcs: FuncMeta[];
  total_packets: number;
  dag_edges: Record<string, string[]>;
  global_max_store_count: number;
  global_max_load_count: number;
  global_max_redundant_count: number;
}

export type NodeTypes = "funcCanvas";
