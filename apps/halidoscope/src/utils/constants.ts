import { AnimationMode } from "@/types";

export const DEFAULT_PLAYBACK_RATE = 10000;
/** Debounce window in ms before a settled scrub position is rendered. */
export const SCRUB_DEBOUNCE_MS = 50;

export const ANIMATION_MODES: AnimationMode[] = ["Blink", "Pulse", "None"];
