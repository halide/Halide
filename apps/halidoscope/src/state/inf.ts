import { atom } from "jotai";

import { AnimationMode } from "@/types";

export const DEFAULT_INF_COLOR = "#ffff00";
export const DEFAULT_INF_ALPHA = 1;

export const infAtom = atom<{
  active: boolean;
  animationMode: AnimationMode;
  color: {
    r: number;
    g: number;
    b: number;
    a: number;
  };
}>({
  active: false,
  animationMode: "Blink",
  color: {
    r: 255,
    g: 255,
    b: 0,
    a: DEFAULT_INF_ALPHA,
  },
});
