export interface FuncStats extends Record<string, unknown> {
  name: string;
  width: number;
  height: number;
  min_coords: number[];
  max_coords: number[];
  min_value: number;
  max_value: number;
  max_store_count: number;
  max_load_count: number;
}

/** Per-channel pixel writes for one color channel of a color func. */
export interface ChannelData {
  xs: number[];
  ys: number[];
  values: number[];
}

/**
 * A single Func's pixel updates for a rendered range, as returned by the
 * backend.
 *
 * @property func The name of the Func being updated.
 * @property xs The x-coordinates of the updated pixels.
 * @property ys The y-coordinates of the updated pixels.
 * @property values Normalized 0-255 values for the updated pixels (Grayscale).
 * @property counts Incremental store counts at each (x, y) for the stores endpoint.
 * @property r The red channel for the updated pixels.
 * @property g The green channel for the updated pixels.
 * @property b The blue channel for the updated pixels.
 */
export interface FuncUpdate {
  func: string;
  xs?: number[];
  ys?: number[];
  values?: number[];
  counts?: number[];
  r?: ChannelData;
  g?: ChannelData;
  b?: ChannelData;
}

/**
 * A range request sent to the backend WebSocket. Renders packets in [start, end).
 */
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

export type NodeTypes = "funcCanvas";
