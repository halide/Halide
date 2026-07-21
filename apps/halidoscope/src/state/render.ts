import { atom } from "jotai";

export const RENDER_MODES = [
  "Grayscale",
  "RGB",
  "Store Frequency",
  "Load Frequency",
  "Redundant Stores",
  "Reuse Distance",
  "Thread Coverage",
] as const;
export type RenderMode = (typeof RENDER_MODES)[number];
export type NormalizationMode = "Across Funcs" | "Per Func";

export const renderAtom = atom<{
  renderMode: RenderMode;
  normalizationMode: NormalizationMode;
}>({ renderMode: "Grayscale", normalizationMode: "Across Funcs" });
