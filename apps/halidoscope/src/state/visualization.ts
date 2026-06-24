import { atom } from "jotai";

export type VisualizationMode =
  | "True Values"
  | "Store Frequency"
  | "Load Frequency"
  | "Redundant Stores"
  | "Reuse Distance";

export const visualizationModeAtom = atom<VisualizationMode>("True Values");
