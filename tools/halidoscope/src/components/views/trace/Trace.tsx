import { ReactFlowProvider } from "@xyflow/react";

import Canvas from "@/components/canvas/Canvas";
import TraceTimeline from "@/components/views/trace/TraceTimeline";
import { useTraceContext } from "@/hooks/trace";
import ControlTabs from "@/components/controls/ControlTabs";

function Trace() {
  const { funcs, dagEdges, packetCount } = useTraceContext();

  return (
    <div className="bg-ps-secondary flex h-full flex-col text-white">
      <div className="relative flex flex-1 overflow-hidden">
        <ReactFlowProvider>
          <Canvas funcs={funcs} dagEdges={dagEdges} />
        </ReactFlowProvider>
        <ControlTabs funcs={funcs} />
      </div>
      <TraceTimeline packetCount={packetCount} />
    </div>
  );
}

export default Trace;
