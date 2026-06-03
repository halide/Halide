import { FuncStats } from "../types";

interface SidebarProps {
  funcs: Record<string, FuncStats>;
}

export function Sidebar({ funcs }: SidebarProps) {
  return (
    <aside className="h-full w-56 bg-slate-800 p-4 overflow-y-auto shrink-0">
      <h2 className="text-sm font-semibold text-slate-400 font-mono uppercase tracking-wider mb-3">
        Funcs
      </h2>
      <ul className="space-y-1">
        {Object.keys(funcs).map((name) => (
          <li key={name} className="text-xs font-mono text-white truncate">
            {name}
          </li>
        ))}
      </ul>
    </aside>
  );
}
