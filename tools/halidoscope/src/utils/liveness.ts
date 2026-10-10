import type { FuncMeta, LiveBox } from "@/types/trace";

/**
 * Determine whether a Func's buffer is live in memory at a given point in the
 * trace.
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
 * The boxes whose packet index range contains a given point in the trace.
 *
 * @param boxes The boxes to filter.
 * @param globalIndex The global packet index to check against.
 */
export function activeBoxes(boxes: LiveBox[], globalIndex: number) {
  return boxes.filter((b) => b.start <= globalIndex && globalIndex <= b.end);
}

function anyActive(boxes: LiveBox[], globalIndex: number) {
  return boxes.some((b) => b.start <= globalIndex && globalIndex <= b.end);
}

/**
 * Determine whether the dataflow edge between a producer and consumer Func is
 * live at a given point in the trace.
 *
 * @param funcs A map from Func name to its metadata.
 * @param source The name of the producer Func.
 * @param target The name of the consumer Func.
 * @param globalIndex The global packet index to check against.
 * @returns Whether `source` is being consumed while `target` is being produced
 * at `globalIndex`.
 */
export function isEdgeLive(
  funcs: Record<string, FuncMeta>,
  source: string,
  target: string,
  globalIndex: number,
) {
  return (
    anyActive(funcs[source].liveness.consumptions, globalIndex) &&
    anyActive(funcs[target].liveness.productions, globalIndex)
  );
}
