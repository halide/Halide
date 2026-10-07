import { atom } from "jotai";

import type { FuncMeta } from "@/types/trace";

export const RENDER_MODES = [
  "Grayscale",
  "Plot",
  "RGB",
  "Store Frequency",
  "Load Frequency",
  "Redundant Stores",
  "Reuse Distance",
  "Thread Coverage",
] as const;
export type RenderMode = (typeof RENDER_MODES)[number];

/** The render modes that display a Func's values, as opposed to statistics about its accesses. */
export function isValueMode(mode: RenderMode): boolean {
  return mode === "Grayscale" || mode === "Plot" || mode === "RGB";
}

/** The render modes offered for a Func. Plot is offered only for Funcs of at most one dimension. */
export function renderModesFor(func: FuncMeta): readonly RenderMode[] {
  return func.min_coords.length <= 1
    ? RENDER_MODES
    : RENDER_MODES.filter((mode) => mode !== "Plot");
}
export type NormalizationMode = "Across Funcs" | "Per Func";

export const renderAtom = atom<{
  normalizationMode: NormalizationMode;
}>({ normalizationMode: "Per Func" });
