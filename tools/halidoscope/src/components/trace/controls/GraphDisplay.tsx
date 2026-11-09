import { type Edge } from "@xyflow/react";
import { useSetAtom } from "jotai";
import * as React from "react";

import Checkbox from "@/components/shared/Checkbox";
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
    <Checkbox
      id="graph-edge"
      checked={visible}
      onCheckedChange={onEdgeVisibilityChange}
      label="Show Graph Edges"
    />
  );
}

export default GraphDisplay;
