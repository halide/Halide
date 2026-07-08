import { BaseEdge, getBezierPath, type EdgeProps } from "@xyflow/react";
import { useAtomValue } from "jotai";
import * as React from "react";

import { useTraceContext } from "@/hooks/trace";
import { livenessAtom } from "@/state/liveness";
import { packetAtom } from "@/state/packet";
import { isEdgeLive } from "@/utils/liveness";

function FuncEdge({
  id,
  source,
  target,
  sourceX,
  sourceY,
  targetX,
  targetY,
  sourcePosition,
  targetPosition,
  style,
  markerStart,
  markerEnd,
}: EdgeProps) {
  const [path] = getBezierPath({
    sourceX,
    sourceY,
    sourcePosition,
    targetX,
    targetY,
    targetPosition,
  });
  const gradientId = `produce-consume-gradient-${id}`;

  const { funcs } = useTraceContext();
  const liveness = useAtomValue(livenessAtom);
  const packetIndex = useAtomValue(packetAtom);
  const isProduceConsumeMode = React.useMemo(() => {
    return (
      liveness.active &&
      liveness.mode === "produce-consume" &&
      isEdgeLive(funcs, source, target, packetIndex)
    );
  }, [liveness, funcs, packetIndex, source, target]);

  return (
    <>
      <defs>
        <linearGradient
          id={gradientId}
          gradientUnits="userSpaceOnUse"
          x1={sourceX}
          y1={sourceY}
          x2={targetX}
          y2={targetY}
        >
          <stop offset="0%" stopColor="var(--color-produce)" />
          <stop offset="100%" stopColor="var(--color-consume)" />
        </linearGradient>
      </defs>
      <BaseEdge
        id={id}
        path={path}
        style={{
          ...style,
          stroke: isProduceConsumeMode
            ? `url(#${gradientId})`
            : "var(--color-ps-text-primary)",
        }}
        markerStart={markerStart}
        markerEnd={markerEnd}
      />
    </>
  );
}

export default FuncEdge;
