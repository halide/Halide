import { atom } from "jotai";

import { AnimationMode } from "@/types";

export const DEFAULT_NAN_COLOR = "#00ffff";
export const DEFAULT_NAN_ALPHA = 1;

export const nanAtom = atom<{
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
    r: 0,
    g: 255,
    b: 255,
    a: DEFAULT_NAN_ALPHA,
  },
});
