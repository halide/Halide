import type { FuncMeta } from "@/types";

export function isFuncBufferLive(func: FuncMeta, globalIndex: number) {
  return (
    func.buffer_liveness.start <= globalIndex &&
    globalIndex <= func.buffer_liveness.end
  );
}

export function isFuncConsuming(func: FuncMeta, globalIndex: number) {
  return func.consume_ranges.some(
    (range) => range.start <= globalIndex && globalIndex <= range.end,
  );
}

export function isFuncProducing(func: FuncMeta, globalIndex: number) {
  return func.produce_ranges.some(
    (range) => range.start <= globalIndex && globalIndex <= range.end,
  );
}

export function isEdgeLive(
  funcs: Record<string, FuncMeta>,
  source: string,
  target: string,
  globalIndex: number,
) {
  return (
    isFuncProducing(funcs[source], globalIndex) &&
    isFuncConsuming(funcs[target], globalIndex)
  );
}
