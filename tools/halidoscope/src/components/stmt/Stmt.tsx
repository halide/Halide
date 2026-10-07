import { useAtomValue, useSetAtom } from "jotai";
import * as React from "react";

import { useOpenFuncMenu } from "@/hooks/funcMenu";
import {
  funcMenuAtom,
  stmtFuncsAtom,
  stmtTargetAtom,
} from "@/state/navigation";

const SELECTED_CLASS = "halidoscope-selected";
const SELECTED_STYLE = `.${SELECTED_CLASS} {
  background: rgba(255, 190, 0, 0.35);
  outline: 2px solid rgb(255, 160, 0);
}`;

/** The label of the first produce node of each Func, by IR name. */
function findProduces(doc: Document): Map<string, Element> {
  const produces = new Map<string, Element>();
  for (const keyword of doc.querySelectorAll("div.Produce span.keyword")) {
    const variable = keyword.nextElementSibling;
    if (
      keyword.textContent?.trim() === "produce" &&
      variable?.matches("b.variable")
    ) {
      const name = variable.textContent ?? "";
      if (!produces.has(name)) {
        produces.set(name, keyword.closest("label") ?? variable);
      }
    }
  }
  return produces;
}

/**
 * The Func a variable belongs to: the longest Func name that is the variable
 * or a dot-separated prefix of it (e.g. `f` for the loop variable `f.s0.x`).
 */
function variableFunc(
  variable: string,
  funcs: Iterable<string>,
): string | null {
  let best: string | null = null;
  for (const name of funcs) {
    if (
      (variable === name || variable.startsWith(`${name}.`)) &&
      name.length > (best?.length ?? -1)
    ) {
      best = name;
    }
  }
  return best;
}

function expandAncestors(element: Element) {
  for (let node = element.parentElement; node; node = node.parentElement) {
    if (node.matches("div.indent")) {
      const toggle = node.parentElement?.querySelector<HTMLInputElement>(
        ":scope > input.show-hide-btn",
      );
      if (toggle) {
        toggle.checked = false;
      }
    }
  }
}

function Stmt({ html }: { html: string }) {
  const iframeRef = React.useRef<HTMLIFrameElement>(null);
  const [doc, setDoc] = React.useState<Document | null>(null);
  const setStmtFuncs = useSetAtom(stmtFuncsAtom);
  const target = useAtomValue(stmtTargetAtom);
  const setMenu = useSetAtom(funcMenuAtom);
  const openMenu = useOpenFuncMenu();

  const produces = React.useMemo(
    () => (doc ? findProduces(doc) : new Map<string, Element>()),
    [doc],
  );

  React.useEffect(() => {
    setStmtFuncs(new Set(produces.keys()));
  }, [produces, setStmtFuncs]);

  React.useEffect(() => {
    if (!doc) {
      return;
    }
    const style = doc.createElement("style");
    style.textContent = SELECTED_STYLE;
    doc.head.appendChild(style);

    const onContextMenu = (event: MouseEvent) => {
      const variable = (event.target as Element | null)?.closest?.(
        "b.variable",
      );
      const name =
        variable && variableFunc(variable.textContent ?? "", produces.keys());
      const rect = iframeRef.current?.getBoundingClientRect();
      if (name && rect) {
        openMenu(
          {
            clientX: event.clientX + rect.left,
            clientY: event.clientY + rect.top,
            preventDefault: () => event.preventDefault(),
          },
          name,
        );
      }
    };
    const closeMenu = () => setMenu(null);
    // WebKitGTK activates labels on non-primary clicks, which would toggle a
    // node's collapse checkbox.
    const suppressAuxClick = (event: MouseEvent) => {
      if (event.button !== 0) {
        event.preventDefault();
      }
    };
    doc.addEventListener("contextmenu", onContextMenu);
    doc.addEventListener("click", suppressAuxClick, true);
    doc.addEventListener("auxclick", suppressAuxClick, true);
    doc.addEventListener("mousedown", closeMenu);
    doc.addEventListener("wheel", closeMenu);
    return () => {
      doc.removeEventListener("contextmenu", onContextMenu);
      doc.removeEventListener("click", suppressAuxClick, true);
      doc.removeEventListener("auxclick", suppressAuxClick, true);
      doc.removeEventListener("mousedown", closeMenu);
      doc.removeEventListener("wheel", closeMenu);
      style.remove();
    };
  }, [doc, produces, openMenu, setMenu]);

  React.useEffect(() => {
    const element = target && produces.get(target.name);
    if (!doc || !element) {
      return;
    }
    for (const selected of doc.querySelectorAll(`.${SELECTED_CLASS}`)) {
      selected.classList.remove(SELECTED_CLASS);
    }
    expandAncestors(element);
    element.classList.add(SELECTED_CLASS);
    element.scrollIntoView({ block: "center" });
  }, [doc, produces, target]);

  return (
    <iframe
      ref={iframeRef}
      srcDoc={html}
      onLoad={(event) => setDoc(event.currentTarget.contentDocument)}
      className="h-full w-full border-0 bg-white"
    />
  );
}

export default Stmt;
