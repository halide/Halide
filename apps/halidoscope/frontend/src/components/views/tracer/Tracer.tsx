import { ReactFlowProvider } from "@xyflow/react";

import Canvas from "@/components/shared/Canvas";
import Timeline from "@/components/views/tracer/TracerTimeline";
import { CanvasRegistryProvider } from "@/hooks/canvas-registry";
import { useTraceContext } from "@/hooks/trace";
import ControlTabs from "@/components/controls/ControlTabs";

function Tracer() {
  const { sessionId, funcs, dagEdges, packetCount, canvasRegistry } =
    useTraceContext();

  return (
    <CanvasRegistryProvider value={canvasRegistry}>
      <div className="h-full w-full bg-ps-secondary flex text-white">
        <div className="flex flex-col flex-1">
          <div className="flex flex-1 relative">
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
          <Timeline
            packetCount={packetCount}
            sessionId={sessionId}
            canvasRegistry={canvasRegistry}
          />
        </div>
      </div>
    </CanvasRegistryProvider>
  );
}

export default Tracer;
