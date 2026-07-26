import { invoke } from "@tauri-apps/api/core";
import { getMatches } from "@tauri-apps/plugin-cli";
import { useSetAtom } from "jotai";
import { Tabs } from "radix-ui";
import * as React from "react";

import Profiler from "@/components/views/profiler/Profiler";
import Tracer from "@/components/views/tracer/Tracer";
import { ProfileContextProvider } from "@/hooks/profile";
import { TraceContextProvider } from "@/hooks/trace";
import { funcAtom } from "@/state/func";
import { Profile, type FuncMeta, type StatsMeta } from "@/types";
import { openProfile, openTrace } from "@/utils/api";

import "./App.css";

async function resolvePath(path: string) {
  return path.startsWith("/")
    ? path
    : `${await invoke<string>("get_cwd")}/${path}`;
}

function App() {
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
  const [profile, setProfile] = React.useState<Profile | null>(null);

  const setActiveFunc = useSetAtom(funcAtom);

  React.useEffect(() => {
    async function loadTraceFromCLI() {
      const matches = await getMatches();
      const tracePath = matches.args.trace?.value;

      if (typeof tracePath !== "string") {
        return;
      }
      const resolvedTracePath = await resolvePath(tracePath);

      try {
        const { funcs, total_packets, dag_edges, stats } =
          await openTrace(resolvedTracePath);

        const byName: Record<string, FuncMeta> = {};
        for (const func of funcs) {
          byName[func.name] = func;
        }

        setFuncs(byName);
        setDagEdges(dag_edges);
        setPacketCount(total_packets);
        setStats(stats);
        setActiveFunc(funcs[0]?.name ?? "");
      } catch (err) {
        console.error("Error loading trace from CLI: ", err);
      }
    }

    loadTraceFromCLI();
  }, [setActiveFunc]);

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
          <main className="h-full w-full">
            <Tracer />
          </main>
        </TraceContextProvider>
      </Tabs.Content>
      {profile !== null ? (
        <Tabs.Content value="profile" className="flex-1 overflow-hidden">
          <ProfileContextProvider value={profile}>
            <main className="h-full w-full">
              <Profiler />
            </main>
          </ProfileContextProvider>
        </Tabs.Content>
      ) : null}
    </Tabs.Root>
  );
}

export default App;
