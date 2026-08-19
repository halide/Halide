import { Separator } from "radix-ui";

import { useProfileContext } from "@/hooks/profile";

function ProfileOverview() {
  const { pipelines } = useProfileContext();

  const {
    name,
    runs,
    billed_runs,
    samples,
    time_ns,
    memory_peak,
    memory_total,
    active_threads_numerator,
    active_threads_denominator,
  } = pipelines[0];

  return (
    <div className="bg-ps-border-primary text-ps-text-primary border-l-ps-border-primary flex h-8 w-full shrink-0 items-center gap-2 px-8 py-2 text-xs">
      <div className="flex gap-1">
        <span className="text-ps-text-primary/60">Pipeline</span>
        <span className="font-semibold">{name}</span>
      </div>
      <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
      <div className="flex gap-1">
        <span className="text-ps-text-primary/60">Runs</span>
        <span className="font-semibold">{runs}</span>
      </div>
      <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
      <div className="flex gap-1">
        <span className="text-ps-text-primary/60">Billed Runs</span>
        <span className="font-semibold">{billed_runs}</span>
      </div>
      <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
      <div className="flex gap-1">
        <span className="text-ps-text-primary/60">Samples</span>
        <span className="font-semibold">{samples}</span>
      </div>
      <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
      <div className="flex gap-1">
        <span className="text-ps-text-primary/60">Avg. Time / Run</span>
        <span className="font-semibold">
          {time_ns / billed_runs}&thinsp;ns /{" "}
          {((time_ns / billed_runs) * 1e-6).toFixed(3)}&thinsp;ms
        </span>
      </div>
      <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
      <div className="flex gap-1">
        <span className="text-ps-text-primary/60">Peak Memory</span>
        <span className="font-semibold">
          {memory_peak}&thinsp;
          <span className="text-oxide-purple">B</span>
          {" / "}
          {(memory_peak * 1e-6).toFixed(3)}&thinsp;
          <span className="text-oxide-yellow">MB</span>
        </span>
      </div>
      <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
      <div className="flex gap-1">
        <span className="text-ps-text-primary/60">Total Memory</span>
        <span className="font-semibold">
          <span className="font-semibold">
            {memory_total / runs}&thinsp;
            <span className="text-oxide-purple">B</span>
            {" / "}
            {((memory_total / runs) * 1e-6).toFixed(3)}&thinsp;
            <span className="text-oxide-yellow">MB</span>
          </span>
        </span>
      </div>
      <Separator.Root className="bg-ps-border-tertiary h-full w-px" />
      <div className="flex gap-1">
        <span className="text-ps-text-primary/60">Avg. Threads</span>
        <span className="font-semibold">
          {(active_threads_numerator / active_threads_denominator).toFixed(3)}
        </span>
      </div>
    </div>
  );
}

export default ProfileOverview;
