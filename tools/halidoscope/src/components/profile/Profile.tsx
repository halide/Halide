import * as d3 from "d3";
import { useAtom } from "jotai";
import * as React from "react";

import Treemap, { type TreemapBin } from "@/components/profile/charts/Treemap";
import ProfileOverview from "@/components/profile/panels/ProfileOverview";
import ProfilePanel from "@/components/profile/panels/ProfilePanel";
import ProfilerTable from "@/components/profile/panels/ProfileTable";
import Select from "@/components/shared/Select";
import { useOpenFuncMenu } from "@/hooks/funcMenu";
import { useProfileContext } from "@/hooks/profile";
import { profileSelectionAtom } from "@/state/navigation";
import type { ProfileFunc } from "@/types/profile";
import { formatBytes, formatTime } from "@/utils/formatters";
import { profileEntryFunc } from "@/utils/profile";

const DIMENSIONS = { width: 720, height: 360 };

// Smallest to largest unit.
const BIN_CLASSES = [
  "fill-oxide-purple/75! stroke-oxide-purple!",
  "fill-oxide-green/75! stroke-oxide-green!",
  "fill-oxide-yellow/75! stroke-oxide-yellow!",
  "fill-oxide-red/75! stroke-oxide-red!",
];

function makeBins(units: string[]): TreemapBin[] {
  return units.map((name, i) => ({ name, className: BIN_CLASSES[i] }));
}

interface Scale {
  bins: TreemapBin[];
  binIndex: (value: number) => number;
  format: (value: number) => string;
}

const BYTES: Scale = {
  bins: makeBins(["B", "KB", "MB", "GB"]),
  binIndex: (bytes) => Math.floor(Math.log(bytes) / Math.log(1024)),
  format: formatBytes,
};

const TIME: Scale = {
  bins: makeBins(["ns", "µs", "ms", "s"]),
  binIndex: (ns) => Math.floor(Math.log10(ns) / 3),
  format: formatTime,
};

const COUNT: Scale = {
  bins: makeBins(["1", "K", "M", "G"]),
  binIndex: (n) => Math.floor(Math.log10(n) / 3),
  format: (n) => d3.format(".3~s")(n).replace("k", "K"),
};

interface Metric {
  label: string;
  scale: Scale;
  /** The value of `func`, given the pipeline's `runs` and `billed_runs`. */
  value: (func: ProfileFunc, runs: number, billedRuns: number) => number;
}

const perRun = (x: number, runs: number) => (runs > 0 ? x / runs : 0);

const METRICS: Record<string, Metric> = {
  time: {
    label: "Run time per run",
    scale: TIME,
    value: (f, _, billedRuns) => perRun(f.time_ns, billedRuns),
  },
  memory_peak: {
    label: "Peak heap memory",
    scale: BYTES,
    value: (f) => f.memory_peak,
  },
  memory_total: {
    label: "Heap allocated per run",
    scale: BYTES,
    value: (f, runs) => perRun(f.memory_total, runs),
  },
  num_allocs: {
    label: "Heap allocations per run",
    scale: COUNT,
    value: (f, runs) => perRun(f.num_allocs, runs),
  },
  stack_peak: {
    label: "Peak stack memory",
    scale: BYTES,
    value: (f) => f.stack_peak,
  },
  bytes_loaded: {
    label: "Bytes loaded per run",
    scale: BYTES,
    value: (f, runs) => perRun(f.bytes_loaded, runs),
  },
  bytes_stored: {
    label: "Bytes stored per run",
    scale: BYTES,
    value: (f, runs) => perRun(f.bytes_stored, runs),
  },
  points_computed: {
    label: "Points computed per run",
    scale: COUNT,
    value: (f, runs) => perRun(f.points_computed, runs),
  },
};

const METRIC_ITEMS = Object.entries(METRICS).map(([value, metric]) => ({
  value,
  label: metric.label,
}));

interface FuncTreemapProps {
  defaultMetric: string;
  highlight: number | null;
  onHighlight: (id: number | null) => void;
}

function FuncTreemap({
  defaultMetric,
  highlight,
  onHighlight,
}: FuncTreemapProps) {
  const { pipelines } = useProfileContext();
  const { funcs, runs, billed_runs } = pipelines[0];
  const [metricKey, setMetricKey] = React.useState(defaultMetric);
  const metric = METRICS[metricKey];
  const openMenu = useOpenFuncMenu();

  const items = React.useMemo(
    () =>
      funcs.map((func) => ({
        name: func.name,
        id: func.canonical_id,
        value: metric.value(func, runs, billed_runs),
      })),
    [funcs, runs, billed_runs, metric],
  );

  return (
    <ProfilePanel
      label={
        <div className="w-56">
          <Select
            value={metricKey}
            onValueChange={setMetricKey}
            items={METRIC_ITEMS}
          />
        </div>
      }
    >
      <Treemap
        dimensions={DIMENSIONS}
        items={items}
        {...metric.scale}
        highlight={highlight}
        onHighlight={onHighlight}
        onItemContextMenu={(event, id) => {
          const name = profileEntryFunc(funcs[id], funcs);
          if (name !== null) {
            openMenu(event, name);
          }
        }}
      />
    </ProfilePanel>
  );
}

function Profile() {
  const [highlight, setHighlight] = useAtom(profileSelectionAtom);
  const toggleHighlight = React.useCallback(
    (id: number | null) => setHighlight((h) => (h === id ? null : id)),
    [setHighlight],
  );

  return (
    <div className="flex h-full flex-col">
      <ProfileOverview />
      <div className="flex flex-1 flex-col overflow-hidden">
        <ProfilePanel
          label="Profiler Output"
          className="basis-1/2"
          contentClassName="items-start overflow-auto p-0"
        >
          <ProfilerTable highlight={highlight} onHighlight={toggleHighlight} />
        </ProfilePanel>
        <div className="flex min-h-0 basis-1/2">
          <FuncTreemap
            defaultMetric="time"
            highlight={highlight}
            onHighlight={toggleHighlight}
          />
          <FuncTreemap
            defaultMetric="memory_peak"
            highlight={highlight}
            onHighlight={toggleHighlight}
          />
        </div>
      </div>
    </div>
  );
}

export default Profile;
