import { atom } from "jotai";

export type LivenessMode = "none" | "realizations" | "produce-consume";

export const livenessAtom = atom<LivenessMode>("none");
