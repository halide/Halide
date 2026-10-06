import {
  applyEdgeChanges,
  ReactFlow,
  useNodesState,
  useReactFlow,
  useViewport,
  type Node,
  type Edge,
  type EdgeChange,
} from "@xyflow/react";
import { useAtom, useAtomValue, useSetAtom } from "jotai";
import * as React from "react";

import FuncEdge from "@/components/trace/canvas/FuncEdge";
import FuncNode from "@/components/trace/canvas/FuncNode";
import Overlay from "@/components/trace/canvas/Overlay";
import ValueStatus from "@/components/trace/canvas/ValueStatus";
import { funcAtom } from "@/state/func";
import { funcViewAtom, getFuncView } from "@/state/funcView";
import { edgesAtom } from "@/state/graph";
import { LIVENESS_LEGEND, livenessAtom } from "@/state/liveness";
import type { FuncMeta } from "@/types/trace";
import { buildEdges, buildNodes, getLayoutedElements } from "@/utils/graph";

const NODE_TYPES = {
  funcNode: FuncNode,
};

const EDGE_TYPES = {
  funcEdge: FuncEdge,
};

interface Props {
  funcs: Record<string, FuncMeta>;
  dagEdges: Record<string, string[]>;
}

function Canvas({ funcs, dagEdges }: Props) {
  const views = useAtomValue(funcViewAtom);
  // Only zoom affects layout, so key the layout on the zooms rather than the whole views.
  const zoomKey = JSON.stringify(
    Object.entries(funcs).map(
      ([name, func]) => getFuncView(views[name], func).zoom,
    ),
  );
  const { nodes: initialNodes, edges: initialEdges } = React.useMemo(() => {
    return getLayoutedElements(
      buildNodes(funcs, "funcNode", views),
      buildEdges(dagEdges, "funcEdge"),
    );
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [funcs, dagEdges, zoomKey]);
  const [nodes, setNodes, onNodesChange] =
    useNodesState<Node<FuncMeta>>(initialNodes);

  React.useEffect(() => {
    setNodes(initialNodes);
  }, [initialNodes, setNodes]);
  const [edges, setEdges] = useAtom(edgesAtom);
  const setFunc = useSetAtom(funcAtom);
  const liveness = useAtomValue(livenessAtom);
  const { zoom } = useViewport();
  const { zoomTo } = useReactFlow();

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
        minZoom={1e-4}
        maxZoom={Infinity}
        fitView
        fitViewOptions={{ padding: 0.1, maxZoom: 2 }}
        proOptions={{ hideAttribution: true }}
        onNodeClick={(_, node) => setFunc(node.data.name)}
      />
      {liveness.active ? (
        <Overlay className="bottom-2 left-2">
          <div className="flex flex-col gap-2">
            {LIVENESS_LEGEND.map(({ color, label }) => (
              <div key={label} className="flex items-center gap-2">
                <div
                  className="h-3 w-3 border-2"
                  style={{ borderColor: color }}
                />
                <span>{label}</span>
              </div>
            ))}
          </div>
        </Overlay>
      ) : null}
      <ValueStatus />
      <Overlay className="right-2 bottom-2">
        <button
          type="button"
          className="cursor-pointer"
          title="Reset to 100%"
          onClick={() => zoomTo(1)}
        >
          Zoom: {Math.round(zoom * 100)}%
        </button>
      </Overlay>
    </div>
  );
}

export default Canvas;
