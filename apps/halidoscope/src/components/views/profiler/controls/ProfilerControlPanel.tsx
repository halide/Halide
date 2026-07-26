import { useAtom } from "jotai";
import { snakeCase } from "lodash-es";
import { Select } from "radix-ui";

import ControlSection from "@/components/controls/ControlSection";
import ArrowDownIcon from "@/components/icons/ArrowDownIcon";
import {
  PROFILE_METRICS,
  type ProfileMetric,
  profileMetricAtom,
} from "@/state/profile-metric";

function ProfilerControlPanel() {
  const [profileMetric, setProfileMetric] = useAtom(profileMetricAtom);

  return (
    <div className="flex min-w-80">
      <div className="bg-ps-border-secondary h-full w-px" />
      <div className="bg-ps-border-primary h-full w-px" />
      <div className="bg-ps-primary text-ps-text-primary border-l-ps-border-primary flex flex-1 flex-col overflow-auto border-l px-3 py-2 text-xs">
        <ControlSection title="Metric">
          <Select.Root
            value={profileMetric}
            onValueChange={(value) => {
              setProfileMetric(value as ProfileMetric);
            }}
          >
            <Select.Trigger
              id="func-select"
              className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary inline-flex h-8 w-full items-center justify-center rounded border px-2 focus:outline-none"
            >
              <span className="truncate">
                <Select.Value />
              </span>
              <Select.Icon className="ml-auto">
                <ArrowDownIcon />
              </Select.Icon>
            </Select.Trigger>
            <Select.Content
              position="popper"
              sideOffset={4}
              className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary z-10 max-h-(--radix-select-content-available-height) w-(--radix-select-trigger-width) rounded border"
            >
              <Select.Viewport>
                {PROFILE_METRICS.map((metric) => (
                  <Select.Item
                    key={metric}
                    value={snakeCase(metric)}
                    className="hover:bg-ps-border-tertiary cursor-pointer p-2 transition-colors"
                  >
                    <Select.ItemText>{metric}</Select.ItemText>
                  </Select.Item>
                ))}
              </Select.Viewport>
            </Select.Content>
          </Select.Root>
        </ControlSection>
      </div>
    </div>
  );
}

export default ProfilerControlPanel;
