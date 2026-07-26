import ProfilerControlPanel from "@/components/views/profiler/controls/ProfilerControlPanel";
import Treemap from "@/components/views/profiler/Treemap";

function Profiler() {
  return (
    <div className="bg-ps-secondary flex h-full w-full">
      <div className="flex h-full flex-1 items-center justify-center">
        <Treemap />
      </div>
      <ProfilerControlPanel />
    </div>
  );
}

export default Profiler;
