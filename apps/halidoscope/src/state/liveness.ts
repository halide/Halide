import { atom } from "jotai";

export type LivenessMode = "realizations" | "produce-consume";

export const livenessAtom = atom<{ active: boolean; mode: LivenessMode }>({
  active: false,
  mode: "realizations",
});
