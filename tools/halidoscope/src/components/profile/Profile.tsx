import AllocationChurnChart from "@/components/profile/charts/AllocationChurnChart";
import MemoryTreemap from "@/components/profile/charts/MemoryTreemap";
import ProfileOverview from "@/components/profile/panels/ProfileOverview";
import ProfilePanel from "@/components/profile/panels/ProfilePanel";
import ProfilerTable from "@/components/profile/panels/ProfileTable";
import RuntimeChart from "@/components/profile/charts/RuntimeChart";
import type { Profile } from "@/types/profile";

function Profile() {
  return (
    <div className="flex h-full flex-col">
      <div className="flex flex-1 flex-col overflow-hidden">
        <div className="flex basis-3/5">
          <ProfilePanel label="Peak Memory by Func" className="basis-[60%]">
            <MemoryTreemap dimensions={{ width: 720, height: 360 }} />
          </ProfilePanel>
          <ProfilePanel label="Allocation Churn" className="basis-[40%]">
            <AllocationChurnChart dimensions={{ width: 600, height: 400 }} />
          </ProfilePanel>
        </div>
        <div className="flex min-h-0 basis-2/5">
          <ProfilePanel
            label="Estimated Share of Run Time (Sampled)"
            className="basis-[60%]"
          >
            <RuntimeChart dimensions={{ width: 720, height: 180 }} />
          </ProfilePanel>
          <ProfilePanel
            label="Profiler Output"
            className="basis-[60%]"
            contentClassName="items-start overflow-auto p-0"
          >
            <ProfilerTable />
          </ProfilePanel>
        </div>
      </div>
      <ProfileOverview />
    </div>
  );
}

export default Profile;
