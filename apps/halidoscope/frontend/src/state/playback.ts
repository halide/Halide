import { atom } from "jotai";

export type PlaybackMode = "normal" | "stores" | "loads";

export const playbackModeAtom = atom<PlaybackMode>("normal");
