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
 * @property r The red channel for the updated pixels.
 * @property g The green channel for the updated pixels.
 * @property b The blue channel for the updated pixels.
 */
export interface FuncUpdate {
  func: string;
  xs?: number[];
  ys?: number[];
  values?: number[];
  r?: ChannelData;
  g?: ChannelData;
  b?: ChannelData;
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
