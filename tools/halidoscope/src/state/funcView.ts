import { atom } from "jotai";

import type { RenderMode } from "@/state/render";
import type { FuncMeta } from "@/types/trace";

/** Per-Func display settings. */
export interface FuncView {
  renderMode: RenderMode;
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

/** The largest height, before zoom, of a Func in Plot mode. */
const MAX_PLOT_HEIGHT = 256;

/** The size at which a Func is displayed. */
export function displaySize(
  func: FuncMeta,
  view: FuncView,
): { width: number; height: number } {
  return {
    width: func.width * view.zoom,
    height:
      (view.renderMode === "Plot"
        ? Math.min(func.width, MAX_PLOT_HEIGHT)
        : func.height) * view.zoom,
  };
}

/** Views the user has modified, keyed by Func name. */
export const funcViewAtom = atom<Record<string, FuncView>>({});

/** Plot for Funcs of up to one dimension, RGB if dim 2 has extent 3 or 4, and Grayscale otherwise. */
function defaultRenderMode(func: FuncMeta): RenderMode {
  if (func.min_coords.length <= 1) {
    return "Plot";
  }
  const channels =
    func.min_coords.length >= 3 ? func.max_coords[2] - func.min_coords[2] : 1;
  return channels === 3 || channels === 4 ? "RGB" : "Grayscale";
}

/**
 * The view for a Func: whatever the user has set, or else the default render mode, the Func's
 * full value range, and the first slice of every non-image dimension.
 */
export function getFuncView(
  modified: FuncView | undefined,
  func: FuncMeta,
): FuncView {
  return (
    modified ?? {
      renderMode: defaultRenderMode(func),
      blackPoint: func.min_value ?? 0,
      whitePoint: func.max_value ?? 255,
      slice: func.min_coords.slice(2),
      zoom: defaultZoom(func),
    }
  );
}
