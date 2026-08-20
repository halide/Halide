/**
 * Represents the extent of a liveness range for a Func.
 */
export interface LivenessRange {
  /** The packet index where a given Func is live. */
  start: number;
  /** The packet index where a given Func is no longer live. */
  end: number;
}

/**
 * Represents top-level metadata for a Func.
 */
export interface FuncMeta extends Record<string, unknown> {
  /** The name of the Func. */
  name: string;
  /** The width of the Func's buffer. */
  width: number;
  /** The height of the Func's buffer. */
  height: number;
  /** The number of channels in the Func's buffer. */
  channels: number;
  /** The minimum coordinate observed along each logical dimension. */
  min_coords: number[];
  /** The maximum (exclusive) coordinate observed along each logical dimension. */
  max_coords: number[];
  /** The minimum value observed across all loads/stores for this Func. */
  min_value: number | null;
  /** The maximum value observed across all loads/stores for this Func. */
  max_value: number | null;
  /** The maximum number of stores observed at any single coordinate for this Func. */
  max_store_count: number;
  /** The maximum number of loads observed at any single coordinate for this Func. */
  max_load_count: number;
  /**
   * The maximum number of redundant stores observed at any single coordinate for this Func.
   *
   * @remarks
   *
   * A store is redundant when the incoming value bit-matches the previously stored
   * value at that coordinate and there are no intervening loads from it.
   */
  max_redundant_store_count: number;
  /**
   * The maximum store-to-load reuse distance observed across all coordinates for this Func.
   *
   * @remarks
   *
   * Measured as the difference in global packet indices between a store and the
   * next load from the same coordinate.
   */
  max_reuse_distance: number;
  /** The packet index range over which this Func's buffer is live in memory. */
  buffer_liveness: LivenessRange;
  /** The packet index ranges during which this Func is being produced. */
  produce_ranges: LivenessRange[];
  /** The packet index ranges during which this Func is being consumed. */
  consume_ranges: LivenessRange[];
  /** The number of distinct threads that executed this Func. */
  thread_count: number;
  /** The IDs of the distinct threads that executed this Func. */
  thread_ids: string[];
}

/**
 * Represents trace-wide statistics aggregated across all Funcs.
 */
export interface StatsMeta {
  /** The maximum store count observed at any coordinate across all Funcs. */
  global_max_store_count: number;
  /** The maximum load count observed at any coordinate across all Funcs. */
  global_max_load_count: number;
  /** The maximum redundant store count observed at any coordinate across all Funcs. */
  global_max_redundant_store_count: number;
  /** The maximum store-to-load reuse distance observed across all Funcs. */
  global_max_reuse_distance: number;
  /** The IDs of every distinct thread observed across all Funcs. */
  global_thread_ids: string[];
}

/**
 * Represents the complete metadata payload for a trace, as returned by the `open_trace` command.
 */
export interface TraceMeta {
  /** Metadata for each Func in the trace. */
  funcs: FuncMeta[];
  /** The total number of packets in the trace. */
  total_packets: number;
  /**
   * A map from each Func's name to the names of the Funcs it consumes (i.e. its producers in the
   * pipeline's DAG).
   */
  dag_edges: Record<string, string[]>;
  /** Trace-wide statistics aggregated across all Funcs. */
  stats: StatsMeta;
}
