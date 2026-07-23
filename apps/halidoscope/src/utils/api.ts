import { invoke } from "@tauri-apps/api/core";

import type { NormalizationMode } from "@/state/render";
import type { ThreadOpMode } from "@/state/thread";
import type { TraceMeta } from "@/types";

export async function openTrace(path: string): Promise<TraceMeta> {
  return invoke<TraceMeta>("open_trace", { path });
}

export interface RenderFuncResponse {
  tensorData: Uint8ClampedArray<ArrayBuffer>;
  tabularData: Uint32Array | null;
}

export interface RenderFuncParams {
  func: string;
  globalIndex: number;
  normalizationMode: NormalizationMode;
  width: number;
  height: number;
  includeTabularData: boolean;
}

export async function renderGrayscale({
  func,
  globalIndex,
  normalizationMode,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_grayscale", {
    func,
    globalIndex,
    normalizationMode,
  });

  return {
    tensorData: new Uint8ClampedArray(buffer),
    tabularData: null,
  };
}

export async function renderRgb({
  func,
  globalIndex,
  normalizationMode,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_rgb", {
    func,
    globalIndex,
    normalizationMode,
  });

  return {
    tensorData: new Uint8ClampedArray(buffer),
    tabularData: null,
  };
}

/**
 * Split the ArrayBuffer returned by {@link RenderFuncResponse} into the tensor
 * data and the (optionally returned) tabular data. We only include tabular
 * data for the actively selected Func.
 *
 * @param buffer The buffer containing tensor data.
 * @param width The width of the buffer.
 * @param height The height of the buffer.
 * @param includeTabularData A flag indicating whether of not to expect tabular
 * data in the buffer payload.
 * @returns A {@link RenderFuncResponse}.
 */
function splitTensorDataAndTabularData({
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

  return {
    tensorData: new Uint8ClampedArray(buffer, 0, pixelByteLength),
    tabularData: includeTabularData
      ? new Uint32Array(buffer, pixelByteLength)
      : null,
  };
}

export async function renderStoreFrequency({
  func,
  globalIndex,
  normalizationMode,
  width,
  height,
  includeTabularData,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_store_frequency", {
    func,
    globalIndex,
    normalizationMode,
    includeTabularData,
  });

  return splitTensorDataAndTabularData({
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
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_load_frequency", {
    func,
    globalIndex,
    normalizationMode,
    includeTabularData,
  });

  return splitTensorDataAndTabularData({
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
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_redundant_stores", {
    func,
    globalIndex,
    normalizationMode,
    includeTabularData,
  });

  return splitTensorDataAndTabularData({
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
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_reuse_distance", {
    func,
    globalIndex,
    normalizationMode,
    includeTabularData,
  });

  return splitTensorDataAndTabularData({
    buffer,
    width,
    height,
    includeTabularData,
  });
}

export async function renderNaN({
  func,
  globalIndex,
  normalizationMode,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_nan", {
    func,
    globalIndex,
    normalizationMode,
  });

  return {
    tensorData: new Uint8ClampedArray(buffer),
    tabularData: null,
  };
}

export async function renderInf({
  func,
  globalIndex,
  normalizationMode,
}: RenderFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_inf", {
    func,
    globalIndex,
    normalizationMode,
  });

  return {
    tensorData: new Uint8ClampedArray(buffer),
    tabularData: null,
  };
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
}: RenderThreadFuncParams): Promise<RenderFuncResponse> {
  const buffer = await invoke<ArrayBuffer>("render_thread", {
    func,
    globalIndex,
    normalizationMode,
    opMode: threadOpMode,
    threadId: threadId,
  });

  return splitTensorDataAndTabularData({
    buffer,
    width,
    height,
    includeTabularData,
  });
}
