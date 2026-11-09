import { atom } from "jotai";

export type Scale = "linear" | "log";

export const tabularDataAtom = atom<{
  tabularData: Uint32Array | null;
  scale: Scale;
}>({
  tabularData: null,
  scale: "linear",
});
