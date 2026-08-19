import { ReactFlowProvider } from "@xyflow/react";

import Canvas from "@/components/trace/canvas/Canvas";
import PanelsTabs from "@/components/trace/panels/PanelsTabs";
import TraceTimeline from "@/components/trace/controls/Timeline";
import { useTraceContext } from "@/hooks/trace";

function Trace() {
  const { funcs, dagEdges, packetCount } = useTraceContext();

  return (
    <div className="bg-ps-secondary flex h-full flex-col text-white">
      <div className="relative flex flex-1 overflow-hidden">
        <ReactFlowProvider>
          <Canvas funcs={funcs} dagEdges={dagEdges} />
        </ReactFlowProvider>
        <PanelsTabs funcs={funcs} />
      </div>
      <TraceTimeline packetCount={packetCount} />
    </div>
  );
}

export default Trace;
