import { atom } from "jotai";

import { AnimationMode } from "@/types";

export const infAtom = atom<{ active: boolean; animationMode: AnimationMode }>({
  active: false,
  animationMode: "Blink",
});
