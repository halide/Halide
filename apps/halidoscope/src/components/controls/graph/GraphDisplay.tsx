import { type Edge } from "@xyflow/react";
import { useSetAtom } from "jotai";
import { Checkbox } from "radix-ui";
import * as React from "react";

import { edgesAtom } from "@/state/graph";

function hideEdge(hidden: boolean) {
  return function handleVisibilityChange(edge: Edge) {
    return {
      ...edge,
      hidden,
    };
  };
}

function GraphDisplay() {
  const [visible, setVisible] = React.useState<boolean>(true);
  const setEdges = useSetAtom(edgesAtom);

  function onEdgeVisibilityChange(checked: boolean) {
    setVisible(checked);
    setEdges((eds) => eds.map(hideEdge(!checked)));
  }

  return (
    <div className="flex items-center gap-2">
      <Checkbox.Root
        className="bg-ps-border-primary border-ps-border-tertiary flex h-4 w-4 items-center justify-center rounded-xs border"
        checked={visible}
        onCheckedChange={onEdgeVisibilityChange}
      >
        <Checkbox.Indicator>
          <svg
            xmlns="http://www.w3.org/2000/svg"
            width="12"
            height="12"
            viewBox="0 0 12 12"
            fill="none"
          >
            <path
              d="M1.5 6L4.5 9L10.5 3"
              stroke="currentColor"
              strokeWidth="1.5"
              strokeLinecap="round"
              strokeLinejoin="round"
            />
          </svg>
        </Checkbox.Indicator>
      </Checkbox.Root>
      <label className="text-ps-text-primary/60">Show Graph Edges</label>
    </div>
  );
}

export default GraphDisplay;
