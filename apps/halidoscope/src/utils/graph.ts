import Dagre from "@dagrejs/dagre";
import type { Node, Edge } from "@xyflow/react";

import { EdgeTypes, FuncMeta, NodeTypes } from "@/types";

/**
 * Build xyflow nodes from the backend's funcs payload, which maps Halide func
 * @param funcs The funcs payload from the backend.
 * @param type The node type to assign to each node.
 * @returns An array of nodes formatted for use with @xyflow/react.
 */
export function buildNodes(
  funcs: Record<string, FuncMeta>,
  type: NodeTypes,
): Node<FuncMeta>[] {
  return Object.entries(funcs).map(([name, stats]) => {
    return {
      id: name,
      type,
      position: {
        x: 0,
        y: 0,
      },
      data: stats,
      style: {
        width: stats.width,
        height: stats.height,
      },
    };
  });
}

/**
 * Build xyflow edges from the backend's dag_edges payload, which maps Halide
 * consumers to their producers.
 *
 * @param dagEdges The dag_edges payload from the backend.
 * @param type The edge type to assign to each edge.
 * @returns An array of edges formatted for use with @xyflow/react.
 */
export function buildEdges(
  dagEdges: Record<string, string[]>,
  type: EdgeTypes,
): Edge[] {
  const edges: {
    id: string;
    source: string;
    target: string;
    type: EdgeTypes;
  }[] = [];

  for (const [producer, consumers] of Object.entries(dagEdges)) {
    for (const consumer of consumers) {
      edges.push({
        id: `${producer}-${consumer}`,
        type,
        source: producer,
        target: consumer,
      });
    }
  }

  return edges;
}

export function getLayoutedElements(
  nodes: Node<FuncMeta>[],
  edges: Edge[],
): { nodes: Node<FuncMeta>[]; edges: Edge[] } {
  const g = new Dagre.graphlib.Graph().setDefaultEdgeLabel(() => ({}));
  g.setGraph({ rankdir: "LR", nodesep: 60, ranksep: 80 });

  edges.forEach((edge) => g.setEdge(edge.source, edge.target));
  nodes.forEach((node) => {
    const width =
      node.measured?.width ?? (node.style?.width as number | undefined) ?? 150;
    const height =
      node.measured?.height ?? (node.style?.height as number | undefined) ?? 50;
    g.setNode(node.id, { ...node, width, height });
  });

  Dagre.layout(g);

  return {
    nodes: nodes.map((node) => {
      const position = g.node(node.id);
      const width =
        node.measured?.width ??
        (node.style?.width as number | undefined) ??
        150;
      const height =
        node.measured?.height ??
        (node.style?.height as number | undefined) ??
        50;
      const x = position.x - width / 2;
      const y = position.y - height / 2;

      return { ...node, position: { x, y } };
    }),
    edges,
  };
}
