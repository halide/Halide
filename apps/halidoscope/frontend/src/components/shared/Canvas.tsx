import {
  ReactFlow,
  useEdgesState,
  useNodesState,
  useViewport,
  type Node,
  type Edge,
} from "@xyflow/react";
import { useAtom } from "jotai";
import * as React from "react";

import FuncCanvas from "@/components/views/tracer/FuncCanvas";
import FuncEdge from "@/components/views/tracer/FuncEdge";
import { FuncStats, NodeTypes } from "@/types";
import { buildEdges, buildNodes, getLayoutedElements } from "@/utils/graph";
import { funcAtom } from "@/state/func";

const NODE_TYPES = {
  funcCanvas: FuncCanvas,
};

const EDGE_TYPES = {
  funcEdge: FuncEdge,
};

function hideEdge(hidden: boolean) {
  return function handleVisibilityChange(edge: Edge) {
    return {
      ...edge,
      hidden,
    };
  };
}

interface CanvasProps {
  funcs: Record<string, FuncStats>;
  dagEdges: Record<string, string[]>;
  type: NodeTypes;
}

function Canvas({ funcs, dagEdges, type }: CanvasProps) {
  const { nodes: initialNodes, edges: initialEdges } = React.useMemo(() => {
    return getLayoutedElements(buildNodes(funcs, type), buildEdges(dagEdges));
  }, [funcs, dagEdges, type]);

  const [nodes, _setNodes, onNodesChange] =
    useNodesState<Node<FuncStats>>(initialNodes);
  const [edges, setEdges, onEdgesChange] = useEdgesState<Edge>(initialEdges);
  const [hidden, _setHidden] = React.useState<boolean>(false);
  const [_func, setFunc] = useAtom(funcAtom);

  const { zoom } = useViewport();

  React.useEffect(() => {
    document.documentElement.style.setProperty("--zoom-level", zoom.toString());
  }, [zoom]);

  React.useEffect(() => {
    setEdges((eds) => eds.map(hideEdge(hidden)));
  }, [hidden, setEdges]);

  return (
    <div className="w-full h-full relative">
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
      <div className="absolute bottom-2 right-2 bg-ps-primary text-ps-text-primary text-xs px-2 py-1 rounded">
        Zoom: {Math.round(zoom * 100)}%
      </div>
    </div>
  );
}

export default Canvas;
