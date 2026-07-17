import { invoke } from "@tauri-apps/api/core";

import type { NormalizationMode } from "@/state/render";
import type { TraceMeta } from "@/types";

export interface RenderResult {
  pixels: Uint8ClampedArray<ArrayBuffer>;
  histogram: Uint32Array | null;
}

export async function openTrace(path: string): Promise<TraceMeta> {
  return invoke<TraceMeta>("open_trace", { path });
}

export async function renderGrayscale(
  func: string,
  globalIndex: number,
  normalizationMode: NormalizationMode,
): Promise<RenderResult> {
  const buffer = await invoke<ArrayBuffer>("render_grayscale", {
    func,
    globalIndex,
    normalizationMode,
  });

  return {
    pixels: new Uint8ClampedArray(buffer),
    histogram: null,
  };
}

export async function renderRgb(
  func: string,
  globalIndex: number,
  normalizationMode: NormalizationMode,
): Promise<RenderResult> {
  const buffer = await invoke<ArrayBuffer>("render_rgb", {
    func,
    globalIndex,
    normalizationMode,
  });

  return {
    pixels: new Uint8ClampedArray(buffer),
    histogram: null,
  };
}

// The backend appends the histogram's bins as little-endian u32s directly after the pixel
// bytes in a single response; split at the known pixel-buffer length to recover both.
function splitPixelsAndHistogram(
  buffer: ArrayBuffer,
  width: number,
  height: number,
): RenderResult {
  const pixelByteLength = width * height * 4;

  return {
    pixels: new Uint8ClampedArray(buffer, 0, pixelByteLength),
    histogram: new Uint32Array(buffer, pixelByteLength),
  };
}

export async function renderStoreFrequency(
  func: string,
  globalIndex: number,
  normalizationMode: NormalizationMode,
  width: number,
  height: number,
): Promise<RenderResult> {
  const buffer = await invoke<ArrayBuffer>("render_store_frequency", {
    func,
    globalIndex,
    normalizationMode,
  });

  return splitPixelsAndHistogram(buffer, width, height);
}

export async function renderLoadFrequency(
  func: string,
  globalIndex: number,
  normalizationMode: NormalizationMode,
  width: number,
  height: number,
): Promise<RenderResult> {
  const buffer = await invoke<ArrayBuffer>("render_load_frequency", {
    func,
    globalIndex,
    normalizationMode,
  });

  return splitPixelsAndHistogram(buffer, width, height);
}

export async function renderRedundantStores(
  func: string,
  globalIndex: number,
  normalizationMode: NormalizationMode,
  width: number,
  height: number,
): Promise<RenderResult> {
  const buffer = await invoke<ArrayBuffer>("render_redundant_stores", {
    func,
    globalIndex,
    normalizationMode,
  });

  return splitPixelsAndHistogram(buffer, width, height);
}

export async function renderReuseDistance(
  func: string,
  globalIndex: number,
  normalizationMode: NormalizationMode,
  width: number,
  height: number,
): Promise<RenderResult> {
  const buffer = await invoke<ArrayBuffer>("render_reuse_distance", {
    func,
    globalIndex,
    normalizationMode,
  });

  return splitPixelsAndHistogram(buffer, width, height);
}

export async function renderNaN(
  func: string,
  globalIndex: number,
  normalizationMode: NormalizationMode,
): Promise<RenderResult> {
  const buffer = await invoke<ArrayBuffer>("render_nan", {
    func,
    globalIndex,
    normalizationMode,
  });

  return {
    pixels: new Uint8ClampedArray(buffer),
    histogram: null,
  };
}

export async function renderInf(
  func: string,
  globalIndex: number,
  normalizationMode: NormalizationMode,
): Promise<RenderResult> {
  const buffer = await invoke<ArrayBuffer>("render_inf", {
    func,
    globalIndex,
    normalizationMode,
  });

  return {
    pixels: new Uint8ClampedArray(buffer),
    histogram: null,
  };
}
