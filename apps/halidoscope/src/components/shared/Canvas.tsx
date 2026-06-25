import {
  applyEdgeChanges,
  ReactFlow,
  useNodesState,
  useViewport,
  type Node,
  type Edge,
  type EdgeChange,
} from "@xyflow/react";
import { useAtom, useAtomValue, useSetAtom } from "jotai";
import * as React from "react";

import FuncCanvas from "@/components/views/tracer/FuncCanvas";
import { funcAtom } from "@/state/func";
import { edgesAtom } from "@/state/graph";
import { livenessAtom } from "@/state/liveness";
import { packetAtom } from "@/state/packet";
import { FuncMeta, NodeTypes } from "@/types";
import { buildEdges, buildNodes, getLayoutedElements } from "@/utils/graph";
import { isFuncConsuming, isFuncProducing } from "@/utils/liveness";

const NODE_TYPES = {
  funcCanvas: FuncCanvas,
};

interface CanvasProps {
  funcs: Record<string, FuncMeta>;
  dagEdges: Record<string, string[]>;
  type: NodeTypes;
}

function Canvas({ funcs, dagEdges, type }: CanvasProps) {
  const { nodes: initialNodes, edges: initialEdges } = React.useMemo(() => {
    return getLayoutedElements(buildNodes(funcs, type), buildEdges(dagEdges));
  }, [funcs, dagEdges, type]);

  const [nodes, _setNodes, onNodesChange] =
    useNodesState<Node<FuncMeta>>(initialNodes);
  const [edges, setEdges] = useAtom(edgesAtom);
  const setFunc = useSetAtom(funcAtom);

  React.useEffect(() => {
    setEdges(initialEdges);
  }, [initialEdges, setEdges]);

  const onEdgesChange = React.useCallback(
    (changes: EdgeChange<Edge>[]) => {
      setEdges((eds) => applyEdgeChanges(changes, eds));
    },
    [setEdges],
  );

  const livenessMode = useAtomValue(livenessAtom);
  const packetIndex = useAtomValue(packetAtom);

  const consumingFuncs = React.useMemo(() => {
    if (livenessMode !== "produce-consume") {
      return new Set<string>();
    }

    const fs = new Set<string>();
    for (const [funcName, func] of Object.entries(funcs)) {
      if (isFuncConsuming(func, packetIndex)) {
        fs.add(funcName);
      }
    }

    return fs;
  }, [livenessMode, funcs, packetIndex]);

  const producingFuncs = React.useMemo(() => {
    if (livenessMode !== "produce-consume") {
      return new Set<string>();
    }

    const fs = new Set<string>();
    for (const [funcName, func] of Object.entries(funcs)) {
      if (isFuncProducing(func, packetIndex)) {
        fs.add(funcName);
      }
    }

    return fs;
  }, [livenessMode, funcs, packetIndex]);

  const styledEdges = React.useMemo(() => {
    return edges.map((edge) => {
      if (consumingFuncs.has(edge.source) && producingFuncs.has(edge.target)) {
        return {
          ...edge,
          style: { stroke: "var(--color-produce)" },
        };
      }

      return edge;
    });
  }, [edges, consumingFuncs, producingFuncs]);

  const { zoom } = useViewport();

  React.useEffect(() => {
    document.documentElement.style.setProperty("--zoom-level", zoom.toString());
  }, [zoom]);

  return (
    <div className="relative h-full w-full">
      <ReactFlow
        nodes={nodes}
        edges={styledEdges}
        nodeTypes={NODE_TYPES}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        minZoom={0.01}
        fitView
        fitViewOptions={{ padding: 0.1 }}
        proOptions={{ hideAttribution: true }}
        onNodeClick={(_, node) => setFunc(node.data.name)}
      />
      <div className="bg-ps-primary text-ps-text-primary absolute right-2 bottom-2 rounded px-2 py-1 text-xs">
        Zoom: {Math.round(zoom * 100)}%
      </div>
    </div>
  );
}

export default Canvas;
