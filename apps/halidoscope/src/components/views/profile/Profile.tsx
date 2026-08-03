import ProfilerControlPanel from "@/components/views/profile/controls/ProfilerControlPanel";
import Treemap from "@/components/views/profile/Treemap";

function Profile() {
  return (
    <div className="bg-ps-secondary flex h-full w-full">
      <div className="flex h-full flex-1 items-center justify-center">
        <Treemap />
      </div>
      <ProfilerControlPanel />
    </div>
  );
}

export default Profile;
