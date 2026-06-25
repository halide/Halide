import { invoke } from "@tauri-apps/api/core";

import type { TraceMeta } from "@/types";

export async function openTrace(path: string): Promise<TraceMeta> {
  return invoke<TraceMeta>("open_trace", { path });
}

export async function renderGrayscale(
  func: string,
  globalIndex: number,
): Promise<ArrayBuffer> {
  return invoke<ArrayBuffer>("render_grayscale", { func, globalIndex });
}

export async function renderRgb(
  func: string,
  globalIndex: number,
): Promise<ArrayBuffer> {
  return invoke<ArrayBuffer>("render_rgb", { func, globalIndex });
}

export async function renderStoreFrequency(
  func: string,
  globalIndex: number,
): Promise<ArrayBuffer> {
  return invoke<ArrayBuffer>("render_store_frequency", { func, globalIndex });
}

export async function renderLoadFrequency(
  func: string,
  globalIndex: number,
): Promise<ArrayBuffer> {
  return invoke<ArrayBuffer>("render_load_frequency", { func, globalIndex });
}

export async function renderRedundantStores(
  func: string,
  globalIndex: number,
): Promise<ArrayBuffer> {
  return invoke<ArrayBuffer>("render_redundant_stores", { func, globalIndex });
}

export async function renderReuseDistance(
  func: string,
  globalIndex: number,
): Promise<ArrayBuffer> {
  return invoke<ArrayBuffer>("render_reuse_distance", { func, globalIndex });
}
