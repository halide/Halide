import { atom } from "jotai";

export type ProfileMetric =
  | "memory_peak"
  | "memory_total"
  | "stack_peak"
  | "num_allocs";

export const PROFILE_METRICS = [
  "Memory Peak",
  "Memory Total",
  "Stack Peak",
  "Num Allocs",
];

export const profileMetricAtom = atom<ProfileMetric>("memory_total");
