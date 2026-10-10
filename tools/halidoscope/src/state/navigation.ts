import type { Viewport } from "@xyflow/react";
import { atom } from "jotai";

export type Tab = "trace" | "profile" | "stmt";

export const tabAtom = atom<Tab>("trace");

/** The canonical id of the Func selected in the profile tab. */
export const profileSelectionAtom = atom<number | null>(null);

/** The trace canvas's viewport, kept across unmounts of the trace tab. */
export const traceViewportAtom = atom<Viewport | null>(null);

/**
 * The Func whose produce node the stmt tab scrolls to. `seq` distinguishes
 * repeated requests for the same Func.
 */
export const stmtTargetAtom = atom<{ name: string; seq: number } | null>(null);

/** The IR names of the Funcs with a produce node in the stmt. */
export const stmtFuncsAtom = atom<ReadonlySet<string>>(new Set<string>());

/** An open context menu for showing a Func, by IR name, in another tab. */
export const funcMenuAtom = atom<{ name: string; x: number; y: number } | null>(
  null,
);
