import { invoke } from "@tauri-apps/api/core";

import type { FuncView } from "@/state/funcView";
import type { NormalizationMode } from "@/state/render";
import type { ThreadOpMode } from "@/state/thread";
import type { Profile } from "@/types/profile";
import type { TraceMeta } from "@/types/trace";

/**
 * Parse a `.hltrace` file and return the metadata needed to set up canvases and
 * the scrub timeline. Replaces any previously loaded trace.
 *
 * @param path The path to the `.hltrace` file to open.
 * @returns The parsed {@link TraceMeta} for the trace.
 */
export async function openTrace(path: string): Promise<TraceMeta> {
  return invoke<TraceMeta>("open_trace", { path });
}

/** The unpacked payload returned by a `render_*` command. */
export interface RenderFuncResponse {
  /** The rendered RGBA8 tensor data for the Func's buffer. */
  tensorData: Uint8ClampedArray<ArrayBuffer>;
  /** The RGBA8 overlay marking coordinates where a NaN was observed. */
  nanOverlayData: Uint8ClampedArray<ArrayBuffer> | null;
  /** The RGBA8 overlay marking coordinates where an Inf was observed. */
  infOverlayData: Uint8ClampedArray<ArrayBuffer> | null;
  /**
   * The 1-bit-per-pixel mask (as packed by `pack_mask` in `render.rs`) of
   * coordinates that have been written, or null if not requested.
   */
  writtenMask: Uint8Array | null;
  /**
   * The per-coordinate tabular data backing histograms, or null if not
   * requested.
   */
  tabularData: Uint32Array | null;
}

/** Shared parameters accepted by every `render_*` command. */
export interface RenderFuncParams {
  /** The name of the Func to render. */
  func: string;
  /** The global packet index to render up to. */
  globalIndex: number;
  /** Whether pixel values are normalized against all Funcs or just this one. */
  normalizationMode: NormalizationMode;
  /** The width of the Func's buffer. */
  width: number;
  /** The height of the Func's buffer. */
  height: number;
  /** The coordinates of logical dims 2 and up, selecting the 2D slice to render. */
  slice: number[];
  /**
   * Whether to compute and return per-coordinate tabular data alongside the
   * rendered pixels.
   */
  includeTabularData: boolean;
  /**
   * The overlay color to apply at coordinates where a NaN was observed,
   * if `active`.
   */
  includeNan: {
    active: boolean;
    r: number;
    g: number;
    b: number;
    a: number;
  };
  /**
   * The overlay color to apply at coordinates where an Inf was observed,
   * if `active`.
   */
  includeInf: {
    active: boolean;
    r: number;
    g: number;
    b: number;
    a: number;
  };
}

/** Parameters accepted by the value (Grayscale / RGB) `render_*` commands. */
export interface ValueRenderFuncParams extends RenderFuncParams {
  /** The black / white point. */
  view: FuncView;
  /** Whether to return the mask of written coordinates. */
  includeWritten?: boolean;
}

/**
 * Expands a 1-bit-per-pixel row-major overlay mask (MSB-first within each
 * byte, as packed by `pack_mask` in `render.rs`) into a dense RGBA8 buffer,
 * painting set pixels with `color` and leaving unset pixels fully transparent.
 *
 * @param mask The packed bitmask, `ceil(width * height / 8)` bytes.
 * @param width The width of the buffer the mask covers.
 * @param height The height of the buffer the mask covers.
 * @param color The color to paint set pixels with.
 * @returns A `width * height * 4`-byte RGBA8 buffer.
 */
function expandMask(
  mask: Uint8Array,
  width: number,
  height: number,
  color: {
    r: number;
    g: number;
    b: number;
    a: number;
  },
): Uint8ClampedArray<ArrayBuffer> {
  const out = new Uint8ClampedArray(width * height * 4);
  const alpha = Math.round(Math.min(1, Math.max(0, color.a)) * 255);

  for (let i = 0; i < width * height; i++) {
    const bit = (mask[i >> 3] >> (7 - (i & 7))) & 1;

    if (bit) {
      const o = i * 4;
      out[o] = color.r;
      out[o + 1] = color.g;
      out[o + 2] = color.b;
      out[o + 3] = alpha;
    }
  }

  return out;
}

/**
 * Splits the ArrayBuffer returned by a render command into the tensor data,
 * NaN/Inf overlays, and the (optionally returned) tabular data. The NaN/Inf
 * overlay planes are 1-bit-per-pixel masks (see `pack_mask` in `render.rs`)
 * expanded here into RGBA8 using the same color the caller requested them in,
 * since every set pixel in a given overlay shares that one color.
 *
 * @param buffer The buffer containing tensor, tabular, and overlay data.
 * @param width The width of the buffer.
 * @param height The height of the buffer.
 * @param includeNan The NaN overlay color, or `null` if not requested.
 * @param includeInf The Inf overlay color, or `null` if not requested.
 * @param includeWritten Whether to expect the written mask in the buffer.
 * @param includeTabularData A flag indicating whether or not to expect tabular
 * data in the buffer payload.
 * @returns A {@link RenderFuncResponse}.
 */
function splitRenderBuffer({
  buffer,
  width,
  height,
  includeNan,
  includeInf,
  includeWritten = false,
  includeTabularData,
}: {
  buffer: ArrayBuffer;
  width: number;
  height: number;
  includeNan: RenderFuncParams["includeNan"] | null;
  includeInf: RenderFuncParams["includeInf"] | null;
  includeWritten?: boolean;
  includeTabularData: boolean;
}): RenderFuncResponse {
  const pixelByteLength = width * height * 4;
  const maskByteLength = Math.ceil((width * height) / 8);

  const nanMaskOffset = pixelByteLength;
  const infMaskOffset = nanMaskOffset + (includeNan ? maskByteLength : 0);
  const writtenMaskOffset = infMaskOffset + (includeInf ? maskByteLength : 0);
  const tabularOffset =
    writtenMaskOffset + (includeWritten ? maskByteLength : 0);
  const tabularByteLength = buffer.byteLength - tabularOffset;

  return {
    tensorData: new Uint8ClampedArray(buffer, 0, pixelByteLength),
    nanOverlayData: includeNan
      ? expandMask(
          new Uint8Array(buffer, nanMaskOffset, maskByteLength),
          width,
          height,
          includeNan,
        )
      : null,
    infOverlayData: includeInf
      ? expandMask(
          new Uint8Array(buffer, infMaskOffset, maskByteLength),
          width,
          height,
          includeInf,
        )
      : null,
    writtenMask: includeWritten
      ? new Uint8Array(buffer, writtenMaskOffset, maskByteLength)
      : null,
    // The masks preceding the tabular data needn't end 4-byte aligned.
    tabularData: includeTabularData
      ? new Uint32Array(
          buffer.slice(tabularOffset, tabularOffset + tabularByteLength),
        )
      : null,
  };
}

/**
 * Render a 2D slice of a Func as a grayscale image at a given packet index.
 *
 * @param params The {@link ValueRenderFuncParams} describing what to render.
 * @returns The {@link RenderFuncResponse} split out from the backend's raw
 * buffer.
 */
export async function renderGrayscale({
  func,
  globalIndex,
  normalizationMode,
  width,
  height,
  slice,
  includeTabularData,
  includeNan,
  includeInf,
  view,
  includeWritten = false,
}: ValueRenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_grayscale", {
    func,
    globalIndex,
    normalizationMode,
    slice,
    blackPoint: view.blackPoint,
    whitePoint: view.whitePoint,
    includeTabularData,
    includeNan: includeNan.active,
    includeInf: includeInf.active,
    includeWritten,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
    includeNan: includeNan.active ? includeNan : null,
    includeInf: includeInf.active ? includeInf : null,
    includeWritten,
    includeTabularData,
  });
}

/** The coordinates and value(s) of a Func at one displayed pixel. */
export interface ProbeResponse {
  /** The coordinate along each logical dim; null for the channel dim in RGB mode. */
  coords: (number | null)[];
  /** The value of each channel as text, or null where never written. */
  values: (string | null)[];
}

/**
 * Look up a Func's value at a displayed pixel as of a given packet index.
 *
 * @returns The {@link ProbeResponse}, or null if the pixel is outside the Func.
 */
export async function probeValue(params: {
  func: string;
  globalIndex: number;
  slice: number[];
  color: boolean;
  x: number;
  y: number;
}): Promise<ProbeResponse | null> {
  return invoke<ProbeResponse | null>("probe_value", params);
}

/**
 * Render a Func as an RGB image at a given packet index. Channels 0/1/2 of
 * dim 2 map to R/G/B, and the view's slice selects dims 3 and up.
 *
 * @param params The {@link ValueRenderFuncParams} describing what to render.
 * @returns The {@link RenderFuncResponse} split out from the backend's raw
 * buffer.
 */
export async function renderRgb({
  func,
  globalIndex,
  normalizationMode,
  width,
  height,
  slice,
  includeTabularData,
  includeNan,
  includeInf,
  view,
}: ValueRenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_rgb", {
    func,
    globalIndex,
    normalizationMode,
    slice,
    blackPoint: view.blackPoint,
    whitePoint: view.whitePoint,
    includeTabularData,
    includeNan: includeNan.active,
    includeInf: includeInf.active,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
    includeNan: includeNan.active ? includeNan : null,
    includeInf: includeInf.active ? includeInf : null,
    includeTabularData,
  });
}

/**
 * Render a heatmap of store counts for a Func at a given packet index.
 *
 * @param params The {@link RenderFuncParams} describing what to render.
 * @returns The {@link RenderFuncResponse} split out from the backend's raw
 * buffer.
 */
export async function renderStoreFrequency({
  func,
  globalIndex,
  normalizationMode,
  width,
  height,
  slice,
  includeTabularData,
  includeNan,
  includeInf,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_store_frequency", {
    func,
    globalIndex,
    normalizationMode,
    slice,
    includeTabularData,
    includeNan: includeNan.active,
    includeInf: includeInf.active,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
    includeNan: includeNan.active ? includeNan : null,
    includeInf: includeInf.active ? includeInf : null,
    includeTabularData,
  });
}

/**
 * Render a heatmap of load counts for a Func at a given packet index.
 *
 * @param params The {@link RenderFuncParams} describing what to render.
 * @returns The {@link RenderFuncResponse} split out from the backend's raw
 * buffer.
 */
export async function renderLoadFrequency({
  func,
  globalIndex,
  normalizationMode,
  width,
  height,
  slice,
  includeTabularData,
  includeNan,
  includeInf,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_load_frequency", {
    func,
    globalIndex,
    normalizationMode,
    slice,
    includeTabularData,
    includeNan: includeNan.active,
    includeInf: includeInf.active,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
    includeNan: includeNan.active ? includeNan : null,
    includeInf: includeInf.active ? includeInf : null,
    includeTabularData,
  });
}

/**
 * Render a heatmap of redundant store counts for a Func at a given packet
 * index.
 *
 * @remarks
 *
 * A store is redundant when it writes the same value to a location that already
 * holds that value _and_ no intervening load has read that value.
 *
 * @param params The {@link RenderFuncParams} describing what to render.
 * @returns The {@link RenderFuncResponse} split out from the backend's raw
 * buffer.
 */
export async function renderRedundantStores({
  func,
  globalIndex,
  normalizationMode,
  width,
  height,
  slice,
  includeTabularData,
  includeNan,
  includeInf,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_redundant_stores", {
    func,
    globalIndex,
    normalizationMode,
    slice,
    includeTabularData,
    includeNan: includeNan.active,
    includeInf: includeInf.active,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
    includeNan: includeNan.active ? includeNan : null,
    includeInf: includeInf.active ? includeInf : null,
    includeTabularData,
  });
}

/**
 * Render a heatmap of maximum store-to-load reuse distances for a Func at a
 * given packet index.
 *
 * @remarks
 *
 * Reuse distance is the number of packets elapsed between a store and the next
 * load from the same location. In the case of input buffers, it is the
 * distance from the first load to the last load from that buffer.
 *
 * @param params The {@link RenderFuncParams} describing what to render.
 * @returns The {@link RenderFuncResponse} split out from the backend's raw
 * buffer.
 */
export async function renderReuseDistance({
  func,
  globalIndex,
  normalizationMode,
  width,
  height,
  slice,
  includeTabularData,
  includeNan,
  includeInf,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_reuse_distance", {
    func,
    globalIndex,
    normalizationMode,
    slice,
    includeTabularData,
    includeNan: includeNan.active,
    includeInf: includeInf.active,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
    includeNan: includeNan.active ? includeNan : null,
    includeInf: includeInf.active ? includeInf : null,
    includeTabularData,
  });
}

/** Parameters accepted by the `render_thread` command. */
export interface RenderThreadFuncParams extends RenderFuncParams {
  /**
   * Whether to render the store or load operations attributed to `threadId`.
   */
  threadOpMode: ThreadOpMode;
  /** The ID of the thread to render coverage for. */
  threadId: string;
}

/**
 * Render a heatmap of the coordinates stored to or loaded from by a single
 * thread for a Func at a given packet index.
 *
 * @param params The {@link RenderThreadFuncParams} describing what to render.
 * @returns The {@link RenderFuncResponse} split out from the backend's raw
 * buffer.
 */
export async function renderThread({
  func,
  globalIndex,
  normalizationMode,
  threadOpMode,
  threadId,
  width,
  height,
  slice,
  includeTabularData,
  includeNan,
  includeInf,
}: RenderThreadFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_thread", {
    func,
    globalIndex,
    normalizationMode,
    slice,
    opMode: threadOpMode,
    threadId,
    includeNan: includeNan.active,
    includeInf: includeInf.active,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
    includeNan: includeNan.active ? includeNan : null,
    includeInf: includeInf.active ? includeInf : null,
    includeTabularData,
  });
}

/**
 * Parse a Halide profiler output file and return its contents.
 *
 * @param path The path to the profiler output file to open.
 * @returns The parsed {@link Profile}.
 */
export async function openStmt(path: string): Promise<string> {
  return invoke<string>("open_stmt", { path });
}

export async function openProfile(path: string): Promise<Profile> {
  return invoke<Profile>("open_profile", { path });
}
