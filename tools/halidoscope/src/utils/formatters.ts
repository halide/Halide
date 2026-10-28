import * as d3 from "d3";

/**
 * Format a byte count with the largest unit (B, KB, MB, GB) that keeps the
 * value at least 1, to 3 significant figures.
 *
 * @param bytes The count of bytes.
 * @returns A formatted byte string.
 */
export function formatBytes(bytes: number): string {
  return formatWithUnits(bytes, 1024, ["B", "KB", "MB", "GB"]);
}

/**
 * Format a duration in nanoseconds with the largest unit (ns, µs, ms, s) that
 * keeps the value at least 1, to 3 significant figures.
 *
 * @param ns The duration in nanoseconds.
 * @returns A formatted duration string.
 */
export function formatTime(ns: number): string {
  return formatWithUnits(ns, 1000, ["ns", "µs", "ms", "s"]);
}

function formatWithUnits(x: number, base: number, units: string[]): string {
  let i = 0;
  while (i < units.length - 1 && x >= base) {
    x /= base;
    i++;
  }
  return `${d3.format(".3~r")(x)}\u2009${units[i]}`;
}
