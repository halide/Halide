import { atom } from "jotai";

export type HistogramScale = "linear" | "log";

export const histogramAtom = atom<HistogramScale>("linear");
