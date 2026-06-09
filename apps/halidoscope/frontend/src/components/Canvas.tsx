import * as React from "react";
import { ReactFlow, useViewport } from "@xyflow/react";

import FuncCanvas from "./FuncCanvas";
import { FuncStats } from "../types";
import { buildEdges, buildNodes, getLayoutedElements } from "../utils/graph";

interface CanvasProps {
  funcs: Record<string, FuncStats>;
  dagEdges: Record<string, string[]>;
}

const NODE_TYPES = {
  funcCanvas: FuncCanvas,
};

function Canvas({ funcs, dagEdges }: CanvasProps) {
  const { nodes, edges } = React.useMemo(() => {
    return getLayoutedElements(buildNodes(funcs), buildEdges(dagEdges));
  }, [funcs, dagEdges]);

  const { zoom } = useViewport();

  React.useEffect(() => {
    document.documentElement.style.setProperty("--zoom-level", zoom.toString());
  }, [zoom]);

  return (
    <div className="w-full h-full">
      <ReactFlow
        nodes={nodes}
        edges={edges}
        nodeTypes={NODE_TYPES}
        minZoom={0.01}
        fitView
        fitViewOptions={{ padding: 0.1 }}
        proOptions={{ hideAttribution: true }}
      />
      <div className="absolute bottom-2 right-2 bg-ps-primary text-ps-text text-xs px-2 py-1 rounded">
        Zoom: {Math.round(zoom * 100)}%
      </div>
    </div>
  );
}

export default Canvas;
