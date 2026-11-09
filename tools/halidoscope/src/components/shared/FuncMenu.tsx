import { useAtom, useAtomValue, useSetAtom } from "jotai";
import * as React from "react";

import { useFuncView } from "@/hooks/funcView";
import { funcAtom } from "@/state/func";
import {
  funcMenuAtom,
  profileSelectionAtom,
  stmtFuncsAtom,
  stmtTargetAtom,
  tabAtom,
  type Tab,
} from "@/state/navigation";
import { renderModesFor } from "@/state/render";
import type { Profile } from "@/types/profile";
import type { FuncMeta } from "@/types/trace";
import { profileFuncId } from "@/utils/profile";

const TAB_LABELS: Record<Tab, string> = {
  trace: "Trace",
  profile: "Profile",
  stmt: "Stmt",
};

interface Props {
  traceFuncs: Record<string, FuncMeta>;
  profile: Profile | null;
}

let stmtTargetSeq = 0;

const ITEM_CLASS =
  "hover:bg-ps-border-tertiary cursor-pointer px-3 py-1 text-left";

function RenderModeItems({
  func,
  onSelect,
}: {
  func: FuncMeta;
  onSelect: () => void;
}) {
  const [view, update] = useFuncView(func);
  return (
    <>
      {renderModesFor(func).map((mode) => (
        <button
          key={mode}
          type="button"
          className={`${ITEM_CLASS} flex gap-2`}
          onClick={() => {
            update({ renderMode: mode });
            onSelect();
          }}
        >
          <span className="w-3">{mode === view.renderMode ? "✓" : ""}</span>
          {mode}
        </button>
      ))}
    </>
  );
}

/** A context menu for showing a Func in the other tabs. */
function FuncMenu({ traceFuncs, profile }: Props) {
  const [menu, setMenu] = useAtom(funcMenuAtom);
  const [tab, setTab] = useAtom(tabAtom);
  const setTraceFunc = useSetAtom(funcAtom);
  const setProfileSelection = useSetAtom(profileSelectionAtom);
  const setStmtTarget = useSetAtom(stmtTargetAtom);
  const stmtFuncs = useAtomValue(stmtFuncsAtom);
  const menuRef = React.useRef<HTMLDivElement>(null);

  React.useEffect(() => {
    if (!menu) {
      return;
    }
    const close = () => setMenu(null);
    // Capture phase, because the trace canvas stops pointer events from bubbling.
    const onPointerDown = (event: PointerEvent) => {
      if (!menuRef.current?.contains(event.target as Node)) {
        close();
      }
    };
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        close();
      }
    };
    window.addEventListener("pointerdown", onPointerDown, true);
    window.addEventListener("wheel", close);
    window.addEventListener("blur", close);
    window.addEventListener("keydown", onKeyDown);
    return () => {
      window.removeEventListener("pointerdown", onPointerDown, true);
      window.removeEventListener("wheel", close);
      window.removeEventListener("blur", close);
      window.removeEventListener("keydown", onKeyDown);
    };
  }, [menu, setMenu]);

  React.useLayoutEffect(() => {
    const el = menuRef.current;
    if (!menu || !el) {
      return;
    }
    el.style.left = `${Math.max(0, Math.min(menu.x, window.innerWidth - el.offsetWidth))}px`;
    el.style.top = `${Math.max(0, Math.min(menu.y, window.innerHeight - el.offsetHeight))}px`;
  }, [menu]);

  if (!menu) {
    return null;
  }

  const { name } = menu;
  const targets: { tab: Tab; show: () => void }[] = [];
  if (name in traceFuncs) {
    targets.push({
      tab: "trace",
      show: () => {
        setTraceFunc(name);
      },
    });
  }
  const profileId = profile ? profileFuncId(profile.pipelines[0], name) : null;
  if (profileId !== null) {
    targets.push({
      tab: "profile",
      show: () => setProfileSelection(profileId),
    });
  }
  if (stmtFuncs.has(name)) {
    targets.push({
      tab: "stmt",
      show: () => setStmtTarget({ name, seq: ++stmtTargetSeq }),
    });
  }
  const items = targets.filter((target) => target.tab !== tab);
  const traceFunc = tab === "trace" ? traceFuncs[name] : undefined;
  if (items.length === 0 && !traceFunc) {
    return null;
  }

  return (
    <div
      ref={menuRef}
      className="bg-ps-border-primary text-ps-text-primary border-ps-border-tertiary fixed z-50 flex min-w-40 flex-col rounded border py-1 text-xs shadow-lg"
      style={{ left: menu.x, top: menu.y }}
      onContextMenu={(event) => event.preventDefault()}
    >
      <div className="text-ps-text-secondary px-3 py-1 font-mono">{name}</div>
      {items.map((item) => (
        <button
          key={item.tab}
          type="button"
          className={ITEM_CLASS}
          onClick={() => {
            item.show();
            setTab(item.tab);
            setMenu(null);
          }}
        >
          Show in {TAB_LABELS[item.tab]}
        </button>
      ))}
      {traceFunc ? (
        <>
          {items.length > 0 ? (
            <div className="border-ps-border-tertiary my-1 border-t" />
          ) : null}
          <RenderModeItems func={traceFunc} onSelect={() => setMenu(null)} />
        </>
      ) : null}
    </div>
  );
}

export default FuncMenu;
