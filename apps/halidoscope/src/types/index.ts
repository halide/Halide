export interface IndexRange {
  start: number;
  end: number;
}

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
  thread_ids: string[];
}

export interface StatsMeta {
  global_max_store_count: number;
  global_max_load_count: number;
  global_max_redundant_store_count: number;
  global_max_reuse_distance: number;
  global_thread_ids: string[];
}

export interface TraceMeta {
  funcs: FuncMeta[];
  total_packets: number;
  dag_edges: Record<string, string[]>;
  stats: StatsMeta;
}

export type NodeTypes = "funcNode";
export type EdgeTypes = "funcEdge";

export type AnimationMode = "Blink" | "Pulse" | "None";
