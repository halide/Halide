import { atom } from "jotai";

/** Default packets replayed in each tick during trace playback. */
export const DEFAULT_PLAYBACK_RATE = 10000;

export const playbackRateAtom = atom<number>(DEFAULT_PLAYBACK_RATE);
