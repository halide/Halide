import * as d3 from "d3";

export type ByteUnit = "B" | "KB" | "MB" | "GB";

const numberStringRegex = /^([\d.]+)\s*(.*)$/;

/**
 * Format a numeric value as a byte string (e.g., 10KB, 1.5MB).
 *
 * @param bytes The count of bytes.
 * @returns A formatted byte string.
 */
export function formatBytes(bytes: number): {
  value: number;
  unit: ByteUnit;
} {
  const formatted = d3.format(".2s")(bytes).replace("k", "K").concat("B");
  const result = formatted.match(numberStringRegex);

  return {
    value: Number(result?.[1]),
    unit: (result?.[2] as ByteUnit) ?? "B",
  };
}
