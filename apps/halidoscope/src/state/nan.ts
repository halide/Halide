import { atom } from "jotai";

import { AnimationMode } from "@/types";

export const nanAtom = atom<{ active: boolean; animationMode: AnimationMode }>({
  active: false,
  animationMode: "Blink",
});
