export const BACKEND_ENDPOINT = "http://localhost:8765";
export const WS_ENDPOINT = "ws://localhost:8765";

/** Packets advanced per playback tick (mirrors neotrace's playback step). */
export const PLAYBACK_STEP = 10000;
/** Playback tick interval in ms (~33fps, mirrors neotrace). */
export const PLAYBACK_INTERVAL_MS = 30;
/** Debounce window in ms before a settled scrub position is rendered. */
export const SCRUB_DEBOUNCE_MS = 50;
