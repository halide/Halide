import { atom } from "jotai";

import type { FuncMeta } from "@/types/trace";

/** Per-Func controls for the value (Grayscale / RGB) render modes. */
export interface FuncView {
  /** The value displayed as black. */
  blackPoint: number;
  /** The value displayed as white. */
  whitePoint: number;
  /** The coordinates of logical dims 2 and up selecting the displayed 2D slice. */
  slice: number[];
  /** The integer factor by which the Func is upsampled (nearest-neighbor) for display. */
  zoom: number;
}

/** The smallest zoom that makes the Func at least 64 pixels along its wider visible axis. */
export function defaultZoom(func: FuncMeta): number {
  const size = Math.max(func.width, func.height);
  return size > 0 ? Math.max(1, Math.ceil(64 / size)) : 1;
}

/** The largest zoom offered: enough for the Func to reach 1024 pixels, and at least 8. */
export function maxZoom(func: FuncMeta): number {
  const size = Math.max(func.width, func.height);
  return Math.max(8, size > 0 ? Math.ceil(1024 / size) : 1);
}

/** Views the user has modified, keyed by Func name. */
export const funcViewAtom = atom<Record<string, FuncView>>({});

/**
 * The view for a Func: whatever the user has set, or else the Func's full value range and the
 * first slice of every non-image dimension.
 */
export function getFuncView(
  modified: FuncView | undefined,
  func: FuncMeta,
): FuncView {
  return (
    modified ?? {
      blackPoint: func.min_value ?? 0,
      whitePoint: func.max_value ?? 255,
      slice: func.min_coords.slice(2),
      zoom: defaultZoom(func),
    }
  );
}
