import { atom } from "jotai";

export type ThreadOpMode = "Store" | "Load" | "All";
export const threadAtom = atom<{ id: number; op: ThreadOpMode }>({
  id: 0,
  op: "Store",
});
