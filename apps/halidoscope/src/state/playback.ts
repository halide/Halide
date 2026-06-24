import { atom } from "jotai";

import { DEFAULT_PLAYBACK_RATE } from "@/utils/constants";

export const playbackRateAtom = atom<number>(DEFAULT_PLAYBACK_RATE);
