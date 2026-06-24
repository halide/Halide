import { type Edge } from "@xyflow/react";
import { atom } from "jotai";

export const edgesAtom = atom<Edge[]>([]);
