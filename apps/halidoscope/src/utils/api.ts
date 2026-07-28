import { invoke } from "@tauri-apps/api/core";

import type { NormalizationMode } from "@/state/render";
import type { ThreadOpMode } from "@/state/thread";
import type { TraceMeta, Profile } from "@/types";

export async function openTrace(path: string): Promise<TraceMeta> {
  return invoke<TraceMeta>("open_trace", { path });
}

export interface RenderFuncResponse {
  tensorData: Uint8ClampedArray<ArrayBuffer>;
  nanOverlayData: Uint8ClampedArray<ArrayBuffer>;
  infOverlayData: Uint8ClampedArray<ArrayBuffer>;
  tabularData: Uint32Array | null;
}

export interface RenderFuncParams {
  func: string;
  globalIndex: number;
  normalizationMode: NormalizationMode;
  width: number;
  height: number;
  includeTabularData: boolean;
  includeNan: {
    active: boolean;
    r: number;
    g: number;
    b: number;
    a: number;
  };
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

export async function renderGrayscale({
  func,
  globalIndex,
  normalizationMode,
  width,
  height,
  includeNan,
  includeInf,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_grayscale", {
    func,
    globalIndex,
    normalizationMode,
    includeNan,
    includeInf,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
    includeTabularData: false,
  });
}

export async function renderRgb({
  func,
  globalIndex,
  normalizationMode,
  width,
  height,
  includeNan,
  includeInf,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_rgb", {
    func,
    globalIndex,
    normalizationMode,
    includeNan,
    includeInf,
  });

  return splitRenderBuffer({
    buffer,
    width,
    height,
    includeTabularData: false,
  });
}

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

export interface RenderThreadFuncParams extends RenderFuncParams {
  threadOpMode: ThreadOpMode;
  threadId: string;
}

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

export async function openProfile(path: string): Promise<Profile> {
  return invoke<Profile>("open_profile", { path });
}
