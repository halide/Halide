import { ReactFlowProvider } from "@xyflow/react";

import Canvas from "@/components/shared/Canvas";
import Timeline from "@/components/views/tracer/TracerTimeline";
import { useTraceContext } from "@/hooks/trace";
import ControlTabs from "@/components/controls/ControlTabs";

function Tracer() {
  const { funcs, dagEdges, packetCount } = useTraceContext();

  return (
    <div className="bg-ps-secondary flex h-full w-full flex-col text-white">
      <div className="relative flex flex-1 overflow-hidden">
        {Object.keys(funcs).length > 0 ? (
          <>
            <ReactFlowProvider>
              <Canvas funcs={funcs} dagEdges={dagEdges} type="funcCanvas" />
            </ReactFlowProvider>
            <ControlTabs funcs={funcs} />
          </>
        ) : (
          <div className="absolute inset-0 flex items-center justify-center">
            <p className="animate-pulse">Loading trace...</p>
          </div>
        )}
      </div>
      <Timeline packetCount={packetCount} />
    </div>
  );
}

export default Tracer;
