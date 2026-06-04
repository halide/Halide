import type { FuncStats } from "../types";

/**
 * Compute the width and height of a function's bounding box based on its min
 * and max coordinates.
 *
 * @param stats The {@link FuncStats} of the Halide func.
 * @returns The computed width and height of the func's bounding box.
 */
export function computeFuncSize(stats: FuncStats): {
  width: number;
  height: number;
} {
  const width = (stats.max_coords[0] ?? 0) - (stats.min_coords[0] ?? 0) || 1;
  const height = (stats.max_coords[1] ?? 0) - (stats.min_coords[1] ?? 0) || 1;

  return { width, height };
}
