import { FuncStats } from "../types";

interface SidebarProps {
  funcs: Record<string, FuncStats>;
}

function Sidebar({ funcs }: SidebarProps) {
  return (
    <div className="flex">
      <aside className="h-full w-56 bg-ps-primary p-4 overflow-y-auto shrink-0 border-r border-ps-border-primary">
        <h2 className="text-sm font-semibold text-ps-text font-mono uppercase tracking-wider mb-3">
          Funcs
        </h2>
        <ul className="space-y-1">
          {Object.keys(funcs).map((name) => (
            <li key={name} className="text-xs font-mono text-ps-text truncate">
              {name}
            </li>
          ))}
        </ul>
      </aside>
      <div className="w-px h-full bg-ps-border-secondary" />
      <div className="w-px h-full bg-ps-border-primary" />
    </div>
  );
}

export default Sidebar;
