import { atom } from "jotai";

/** The Func and displayed pixel (relative to the Func's min corner) under the mouse, if any. */
export interface Hover {
  func: string;
  x: number;
  y: number;
}

export const hoverAtom = atom<Hover | null>(null);
