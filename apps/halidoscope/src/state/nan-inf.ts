import { atom } from "jotai";

export const ANIMATION_MODES = ["Blink", "Pulse", "None"] as const;
export type AnimationMode = (typeof ANIMATION_MODES)[number];

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
