import { invoke } from "@tauri-apps/api/core";

import type { RenderMode, TraceMeta } from "../types";
import type { VisualizationMode } from "../state/visualization";

/**
 * Parse a Halide trace from disk and return its metadata.
 *
 * @param path Absolute path to the `.hltrace` file.
 * @returns The {@link TraceMeta} describing every Func and the global timeline.
 * @throws If the backend fails to read or parse the trace.
 */
export async function openTrace(path: string): Promise<TraceMeta> {
  return invoke<TraceMeta>("open_trace", { path });
}

/**
 * Render a Func's framebuffer state at a point on the global timeline.
 *
 * The backend accumulates the Func's stores up to `globalIndex` and returns a
 * `width * height * 4` RGBA8 buffer (delivered as an `ArrayBuffer`), ready to
 * hand to `putImageData`.
 *
 * @param func The qualified Func name.
 * @param globalIndex Position on the global packet timeline.
 * @param mode Optional override of the Func's inferred render mode.
 * @returns The raw RGBA8 bytes for the frame.
 */
export async function renderAt(
  func: string,
  globalIndex: number,
  mode?: RenderMode,
): Promise<ArrayBuffer> {
  return invoke<ArrayBuffer>("render_at", { func, globalIndex, mode });
}

/**
 * Render a heatmap of store or load counts for `func` up to `globalIndex`.
 * The mode must be "Store Frequency" or "Load Frequency" — the string is
 * passed directly to the Rust backend. Returns a `width * height * 4` RGBA8
 * buffer with the inferno colormap applied.
 */
export async function renderHeatmap(
  func: string,
  globalIndex: number,
  mode: Exclude<VisualizationMode, "True Values" | "Redundant Stores" | "Reuse Distance">,
): Promise<ArrayBuffer> {
  return invoke<ArrayBuffer>("render_heatmap", { func, globalIndex, mode });
}

/**
 * Render a heatmap of redundant store counts for `func` up to `globalIndex`.
 * A store is redundant when it writes the same value to a location that already
 * holds that value. Returns a `width * height * 4` RGBA8 buffer with the Reds
 * colormap applied; pixels with zero redundant stores are black.
 */
export async function renderRedundant(
  func: string,
  globalIndex: number,
): Promise<ArrayBuffer> {
  return invoke<ArrayBuffer>("render_redundant", { func, globalIndex });
}

/**
 * Render a heatmap of maximum store-to-load reuse distances for `func` up to
 * `globalIndex`. Reuse distance is measured in total packets elapsed between a
 * store and the next load from the same (x, y, channel). Returns a
 * `width * height * 4` RGBA8 buffer; pixels with no store→load pair are black,
 * positive distances map through the Inferno colormap.
 */
export async function renderReuseDistance(
  func: string,
  globalIndex: number,
): Promise<ArrayBuffer> {
  return invoke<ArrayBuffer>("render_reuse_distance", { func, globalIndex });
}
