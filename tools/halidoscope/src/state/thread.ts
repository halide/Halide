import { atom } from "jotai";

export type ThreadOpMode = "Store" | "Load";
export const NO_THREAD_INFO_SENTINEL_ID = "-1";
export const threadAtom = atom<{ id: string; op: ThreadOpMode }>({
  id: NO_THREAD_INFO_SENTINEL_ID,
  op: "Store",
});
