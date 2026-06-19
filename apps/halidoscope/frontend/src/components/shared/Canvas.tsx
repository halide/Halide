import {
  ReactFlow,
  useEdgesState,
  useNodesState,
  useViewport,
  type Node,
  type Edge,
} from "@xyflow/react";
import { useSetAtom } from "jotai";
import * as React from "react";

import FuncCanvas from "@/components/views/tracer/FuncCanvas";
import { FuncMeta, NodeTypes } from "@/types";
import { buildEdges, buildNodes, getLayoutedElements } from "@/utils/graph";
import { funcAtom } from "@/state/func";

const NODE_TYPES = {
  funcCanvas: FuncCanvas,
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
  const [edges, setEdges, onEdgesChange] = useEdgesState<Edge>(initialEdges);
  const [hidden, _setHidden] = React.useState<boolean>(false);
  const setFunc = useSetAtom(funcAtom);

  const { zoom } = useViewport();

  React.useEffect(() => {
    document.documentElement.style.setProperty("--zoom-level", zoom.toString());
  }, [zoom]);

  React.useEffect(() => {
    setEdges((eds) => eds.map(hideEdge(hidden)));
  }, [hidden, setEdges]);

  return (
    <div className="relative h-full w-full">
      <ReactFlow
        nodes={nodes}
        edges={edges}
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
