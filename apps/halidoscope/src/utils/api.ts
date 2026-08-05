import { invoke } from "@tauri-apps/api/core";

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
  nanOverlayData: Uint8ClampedArray<ArrayBuffer>;
  /** The RGBA8 overlay marking coordinates where an Inf was observed. */
  infOverlayData: Uint8ClampedArray<ArrayBuffer>;
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

/**
 * Splits the ArrayBuffer returned by a render command into the tensor data,
 * NaN/Inf overlays, and the (optionally returned) tabular data.
 *
 * @param buffer The buffer containing tensor, tabular, and overlay data.
 * @param width The width of the buffer.
 * @param height The height of the buffer.
 * @param includeTabularData A flag indicating whether or not to expect tabular
 * data in the buffer payload.
 * @returns A {@link RenderFuncResponse}.
 */
function splitRenderBuffer({
  buffer,
  width,
  height,
  includeTabularData,
}: {
  buffer: ArrayBuffer;
  width: number;
  height: number;
  includeTabularData: boolean;
}): RenderFuncResponse {
  const pixelByteLength = width * height * 4;
  const overlayPlaneByteLength = width * height * 4;
  const tabularByteLength =
    buffer.byteLength - pixelByteLength - overlayPlaneByteLength * 2;

  return {
    tensorData: new Uint8ClampedArray(buffer, 0, pixelByteLength),
    nanOverlayData: new Uint8ClampedArray(
      buffer,
      pixelByteLength,
      overlayPlaneByteLength,
    ),
    infOverlayData: new Uint8ClampedArray(
      buffer,
      pixelByteLength + overlayPlaneByteLength,
      overlayPlaneByteLength,
    ),
    tabularData: includeTabularData
      ? new Uint32Array(
          buffer,
          pixelByteLength + 2 * overlayPlaneByteLength,
          tabularByteLength / 4,
        )
      : null,
  };
}

/**
 * Render a Func as a grayscale image at a given packet index.
 *
 * @param params The {@link RenderFuncParams} describing what to render.
 * @returns The {@link RenderFuncResponse} split out from the backend's raw
 * buffer.
 */
export async function renderGrayscale({
  func,
  globalIndex,
  normalizationMode,
  width,
  height,
  includeTabularData,
  includeNan,
  includeInf,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_grayscale", {
    func,
    globalIndex,
    normalizationMode,
    includeTabularData,
    includeNan,
    includeInf,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
    includeTabularData,
  });
}

/**
 * Render a Func as an RGB image at a given packet index. Channels 0/1/2 map to
 * R/G/B.
 *
 * @param params The {@link RenderFuncParams} describing what to render.
 * @returns The {@link RenderFuncResponse} split out from the backend's raw
 * buffer.
 */
export async function renderRgb({
  func,
  globalIndex,
  normalizationMode,
  width,
  height,
  includeTabularData,
  includeNan,
  includeInf,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_rgb", {
    func,
    globalIndex,
    normalizationMode,
    includeTabularData,
    includeNan,
    includeInf,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
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
  includeTabularData,
  includeNan,
  includeInf,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_store_frequency", {
    func,
    globalIndex,
    normalizationMode,
    includeTabularData,
    includeNan,
    includeInf,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
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
  includeTabularData,
  includeNan,
  includeInf,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_load_frequency", {
    func,
    globalIndex,
    normalizationMode,
    includeTabularData,
    includeNan,
    includeInf,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
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
  includeTabularData,
  includeNan,
  includeInf,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_redundant_stores", {
    func,
    globalIndex,
    normalizationMode,
    includeTabularData,
    includeNan,
    includeInf,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
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
 * load from the same (x, y, channel). In the case of input buffers, it is the
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
  includeTabularData,
  includeNan,
  includeInf,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_reuse_distance", {
    func,
    globalIndex,
    normalizationMode,
    includeTabularData,
    includeNan,
    includeInf,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
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
  includeTabularData,
  includeNan,
  includeInf,
}: RenderThreadFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_thread", {
    func,
    globalIndex,
    normalizationMode,
    opMode: threadOpMode,
    threadId: threadId,
    includeNan,
    includeInf,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
    includeTabularData,
  });
}

/**
 * Parse a Halide profiler output file and return its contents.
 *
 * @param path The path to the profiler output file to open.
 * @returns The parsed {@link Profile}.
 */
export async function openProfile(path: string): Promise<Profile> {
  return invoke<Profile>("open_profile", { path });
}
