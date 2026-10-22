import { atom } from "jotai";

/** The display refresh rate assumed for playback, which advances one tick per frame. */
const TICKS_PER_SECOND = 60;

/** The playback rate (packets per tick) at which a trace plays in about `seconds`, to two significant figures. */
export function playbackRateFor(packetCount: number, seconds = 5): number {
  const rate = packetCount / (seconds * TICKS_PER_SECOND);
  if (rate < 1) {
    return 1;
  }
  const unit = Math.max(1, 10 ** (Math.floor(Math.log10(rate)) - 1));
  return Math.round(rate / unit) * unit;
}

export const playbackRateAtom = atom<number>(1);

/** Whether the trace is playing. */
export const playingAtom = atom<boolean>(false);
