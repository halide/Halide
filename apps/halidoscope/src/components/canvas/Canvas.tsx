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

import FuncEdge from "@/components/canvas/FuncEdge";
import FuncNode from "@/components/canvas/FuncNode";
import Overlay from "@/components/canvas/Overlay";
import { funcAtom } from "@/state/func";
import { edgesAtom } from "@/state/graph";
import { livenessAtom } from "@/state/liveness";
import { FuncMeta } from "@/types";
import { buildEdges, buildNodes, getLayoutedElements } from "@/utils/graph";

const NODE_TYPES = {
  funcNode: FuncNode,
};

const EDGE_TYPES = {
  funcEdge: FuncEdge,
};

interface CanvasProps {
  funcs: Record<string, FuncMeta>;
  dagEdges: Record<string, string[]>;
}

function Canvas({ funcs, dagEdges }: CanvasProps) {
  const { nodes: initialNodes, edges: initialEdges } = React.useMemo(() => {
    return getLayoutedElements(
      buildNodes(funcs, "funcNode"),
      buildEdges(dagEdges, "funcEdge"),
    );
  }, [funcs, dagEdges]);
  const [nodes, _setNodes, onNodesChange] =
    useNodesState<Node<FuncMeta>>(initialNodes);
  const [edges, setEdges] = useAtom(edgesAtom);
  const setFunc = useSetAtom(funcAtom);
  const liveness = useAtomValue(livenessAtom);
  const { zoom } = useViewport();

  React.useEffect(() => {
    setEdges(initialEdges);
  }, [initialEdges, setEdges]);

  const onEdgesChange = React.useCallback(
    (changes: EdgeChange<Edge>[]) => {
      setEdges((eds) => applyEdgeChanges(changes, eds));
    },
    [setEdges],
  );

  React.useEffect(() => {
    document.documentElement.style.setProperty("--zoom-level", zoom.toString());
  }, [zoom]);

  return (
    <div className="relative h-full w-full">
      <ReactFlow
        nodes={nodes}
        edges={edges}
        nodeTypes={NODE_TYPES}
        edgeTypes={EDGE_TYPES}
        onNodesChange={onNodesChange}
        onEdgesChange={onEdgesChange}
        minZoom={0.01}
        fitView
        fitViewOptions={{ padding: 0.1 }}
        proOptions={{ hideAttribution: true }}
        onNodeClick={(_, node) => setFunc(node.data.name)}
      />
      {liveness.active ? (
        <Overlay className="bottom-2 left-2">
          {liveness.mode === "realizations" ? (
            <div className="flex items-center gap-2">
              <div className="ring-realization/30 h-3 w-3 ring-2">
                <div className="ring-realization h-full w-full ring-1" />
              </div>
              <span>Buffer Live in Memory</span>
            </div>
          ) : (
            <div className="flex flex-col gap-2">
              <div className="flex items-center gap-2">
                <div className="ring-produce/30 h-3 w-3 ring-2">
                  <div className="ring-produce h-3 w-3 ring-1" />
                </div>
                <span>Producer</span>
              </div>
              <div className="flex items-center gap-2">
                <div className="ring-consume/30 h-3 w-3 ring-2">
                  <div className="ring-consume h-3 w-3 ring-1" />
                </div>
                <span>Consumer</span>
              </div>
            </div>
          )}
        </Overlay>
      ) : null}
      <Overlay className="right-2 bottom-2">
        Zoom: {Math.round(zoom * 100)}%
      </Overlay>
    </div>
  );
}

export default Canvas;
