import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { getMatches } from "@tauri-apps/plugin-cli";
import { useSetAtom } from "jotai";
import { Tabs } from "radix-ui";
import * as React from "react";

import Profile from "@/components/profile/Profile";
import Trace from "@/components/trace/Trace";
import TraceUpload from "@/components/trace/TraceUpload";
import TraceLoading from "@/components/trace/TraceLoading";
import { ProfileContextProvider } from "@/hooks/profile";
import { TraceContextProvider } from "@/hooks/trace";
import { funcAtom } from "@/state/func";
import type { Profile as Pfile } from "@/types/profile";
import type { FuncMeta, StatsMeta } from "@/types/trace";
import { openProfile, openTrace } from "@/utils/api";

import "./App.css";

async function resolvePath(path: string) {
  return path.startsWith("/")
    ? path
    : `${await invoke<string>("get_cwd")}/${path}`;
}

class TracePathError extends Error {
  public readonly name: string;

  constructor(message: string) {
    super(message);

    this.name = "TracePathError";
    Object.setPrototypeOf(this, TracePathError.prototype);
  }
}

enum TraceLoadingState {
  Loading,
  Loaded,
  NeedsUpload,
}

function App() {
  // Loading state.
  const [traceLoading, setTraceLoading] = React.useState<{
    state: TraceLoadingState;
    message: string;
    progress: number;
  }>({
    state: TraceLoadingState.Loading,
    message: "Loading trace...",
    progress: 0,
  });

  // Trace state.
  const [funcs, setFuncs] = React.useState<Record<string, FuncMeta>>({});
  const [dagEdges, setDagEdges] = React.useState<Record<string, string[]>>({});
  const [packetCount, setPacketCount] = React.useState<number>(0);
  const [stats, setStats] = React.useState<StatsMeta>({
    global_max_store_count: 0,
    global_max_load_count: 0,
    global_max_redundant_store_count: 0,
    global_max_reuse_distance: 0,
    global_thread_ids: [],
  });

  // Profile state.
  const [profile, setProfile] = React.useState<Pfile | null>(null);

  // GUI state.
  const setActiveFunc = useSetAtom(funcAtom);

  const loadTrace = React.useCallback(
    async (path: string) => {
      setTraceLoading({
        state: TraceLoadingState.Loading,
        message: "Loading trace...",
        progress: 0,
      });

      const unlisten = await listen<{ progress: number; message: string }>(
        "trace-load-progress",
        (event) => {
          setTraceLoading((prev) => ({
            ...prev,
            progress: event.payload.progress,
            message: event.payload.message,
          }));
        },
      );

      try {
        const { funcs, total_packets, dag_edges, stats } =
          await openTrace(path);

        const byName: Record<string, FuncMeta> = {};
        for (const func of funcs) {
          byName[func.name] = func;
        }

        setFuncs(byName);
        setDagEdges(dag_edges);
        setPacketCount(total_packets);
        setStats(stats);
        setActiveFunc(funcs[0]?.name ?? "");
      } finally {
        unlisten();
        setTraceLoading({
          state: TraceLoadingState.Loaded,
          message: "Trace loaded...",
          progress: 100,
        });
      }
    },
    [setActiveFunc],
  );

  React.useEffect(() => {
    async function loadTraceFromCLI() {
      try {
        const matches = await getMatches();
        const tracePath = matches.args.trace?.value;

        if (typeof tracePath !== "string") {
          throw new TracePathError(
            `Unexpected value for trace path: ${tracePath}`,
          );
        }

        const resolvedTracePath = await resolvePath(tracePath);
        await loadTrace(resolvedTracePath);
      } catch (err) {
        if (err instanceof TracePathError) {
          setTraceLoading((prev) => ({
            ...prev,
            state: TraceLoadingState.NeedsUpload,
          }));
        } else {
          console.error("Error loading trace: ", err);
        }
      }
    }

    loadTraceFromCLI();
  }, [loadTrace]);

  React.useEffect(() => {
    async function loadProfileFromCLI() {
      const matches = await getMatches();
      const profilePath = matches.args.profile?.value;

      if (typeof profilePath !== "string" || !profilePath) {
        return;
      }
      const resolvedProfilePath = await resolvePath(profilePath);

      try {
        const { pipelines } = await openProfile(resolvedProfilePath);
        setProfile({ pipelines });
      } catch (err) {
        console.error("Error loading profile from CLI: ", err);
      }
    }

    loadProfileFromCLI();
  }, []);

  const renderTrace = React.useCallback(() => {
    switch (traceLoading.state) {
      case TraceLoadingState.Loading:
        return (
          <TraceLoading
            progress={traceLoading.progress}
            message={traceLoading.message}
          />
        );
      case TraceLoadingState.NeedsUpload:
        return <TraceUpload onUpload={loadTrace} />;
      case TraceLoadingState.Loaded:
        return <Trace />;
    }
  }, [traceLoading, loadTrace]);

  return (
    <Tabs.Root className="flex h-screen w-screen flex-col" defaultValue="trace">
      <Tabs.List className="bg-ps-titlebar border-ps-border-primary flex border-y">
        <Tabs.Trigger
          value="trace"
          className="data-[state=active]:bg-ps-primary data-[state=inactive]:bg-ps-titlebar data-[state=active]:text-ps-text-primary data-[state=inactive]:text-ps-text-secondary border-ps-border-primary border-r px-3 py-1 text-base font-semibold"
        >
          Trace
        </Tabs.Trigger>
        {profile !== null ? (
          <Tabs.Trigger
            value="profile"
            className="data-[state=active]:bg-ps-primary data-[state=inactive]:bg-ps-titlebar data-[state=active]:text-ps-text-primary data-[state=inactive]:text-ps-text-secondary border-ps-border-primary border-r px-3 py-1 text-base font-semibold"
          >
            Profile
          </Tabs.Trigger>
        ) : null}
      </Tabs.List>
      <Tabs.Content value="trace" className="flex-1 overflow-hidden">
        <TraceContextProvider
          value={{
            funcs,
            dagEdges,
            packetCount,
            stats,
          }}
        >
          <main className="h-full w-full">{renderTrace()}</main>
        </TraceContextProvider>
      </Tabs.Content>
      {profile !== null ? (
        <Tabs.Content value="profile" className="flex-1 overflow-hidden">
          <ProfileContextProvider value={profile}>
            <main className="h-full w-full">
              <Profile />
            </main>
          </ProfileContextProvider>
        </Tabs.Content>
      ) : null}
    </Tabs.Root>
  );
}

export default App;
