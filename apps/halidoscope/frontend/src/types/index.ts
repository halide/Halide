export interface FuncStats {
  name: string;
  min_coords: number[];
  max_coords: number[];
  min_value: number;
  max_value: number;
}

export interface NodeData extends Record<string, unknown>, FuncStats {
  width: number;
  height: number;
}

/**
 * A single func's pixel updates for a rendered range, as returned by the
 * backend WebSocket. `values` are pre-normalized to 0-255 grayscale, and
 * `xs`/`ys` are coordinates within the func's bounding box. `func` is the raw
 * (unqualified) Halide func name.
 */
export interface FuncUpdate {
  func: string;
  xs: number[];
  ys: number[];
  values: number[];
}

/** A range request sent to the backend WebSocket. Renders stores in [start, end). */
export interface RangeRequest {
  start: number;
  end: number;
}

/** The backend's response to a {@link RangeRequest}. */
export interface RenderResponse {
  updates: FuncUpdate[];
  done: boolean;
  start: number;
  end: number;
}
