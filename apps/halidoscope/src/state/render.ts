import { atom } from "jotai";

export const RENDER_MODES = [
  "Grayscale",
  "RGB",
  "Store Frequency",
  "Load Frequency",
  "Redundant Stores",
  "Reuse Distance",
] as const;
export type RenderMode = (typeof RENDER_MODES)[number];

export const renderModeAtom = atom<RenderMode>("Grayscale");
