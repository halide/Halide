import type { FuncMeta } from "@/types/trace";

/**
 * Determine whether a Func's buffer is live in memory at a given point in the
 * åtrace.
 *
 * @param func The Func metadata to check.
 * @param globalIndex The global packet index to check liveness at.
 * @returns Whether the Func's buffer is live at `globalIndex`.
 */
export function isFuncBufferLive(func: FuncMeta, globalIndex: number) {
  return (
    func.buffer_liveness.start <= globalIndex &&
    globalIndex <= func.buffer_liveness.end
  );
}

/**
 * Determine whether a Func is being consumed at a given point in the trace.
 *
 * @param func The Func metadata to check.
 * @param globalIndex The global packet index to check against.
 * @returns Whether `globalIndex` falls within one of the Func's consume ranges.
 */
export function isFuncConsuming(func: FuncMeta, globalIndex: number) {
  return func.consume_ranges.some(
    (range) => range.start <= globalIndex && globalIndex <= range.end,
  );
}

/**
 * Determine whether a Func is being produced at a given point in the trace.
 *
 * @param func The Func metadata to check.
 * @param globalIndex The global packet index to check against.
 * @returns Whether `globalIndex` falls within one of the Func's produce ranges.
 */
export function isFuncProducing(func: FuncMeta, globalIndex: number) {
  return func.produce_ranges.some(
    (range) => range.start <= globalIndex && globalIndex <= range.end,
  );
}

/**
 * Determine whether the dataflow edge between a producer and consumer Func is
 * live at a given point in the trace.
 *
 * @param funcs A map from Func name to its metadata.
 * @param source The name of the producer Func.
 * @param target The name of the consumer Func.
 * @param globalIndex The global packet index to check against.
 * @returns Whether `source` is producing and `target` is consuming at
 * `globalIndex`.
 */
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
