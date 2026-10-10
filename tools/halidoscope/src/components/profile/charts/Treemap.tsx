import * as d3 from "d3";
import { Tooltip } from "radix-ui";
import * as React from "react";

export type TreemapNode = {
  name: string;
  id?: number;
  value?: number;
  children?: TreemapNode[];
};

/** A group of leaves, drawn with `className`. */
export interface TreemapBin {
  name: string;
  className: string;
}

interface Props {
  dimensions: {
    width: number;
    height: number;
  };
  items: { name: string; id: number; value: number }[];
  bins: TreemapBin[];
  /** The index into `bins` of a positive value. */
  binIndex: (value: number) => number;
  format: (value: number) => string;
  /** The id of the highlighted item, or null. */
  highlight: number | null;
  /** Called with the id of a clicked item. */
  onHighlight: (id: number | null) => void;
  onItemContextMenu: (event: React.MouseEvent, id: number) => void;
}

function Treemap({
  dimensions,
  items,
  bins,
  binIndex,
  format,
  highlight,
  onHighlight,
  onItemContextMenu,
}: Props) {
  const id = React.useId();

  const root = React.useMemo(() => {
    const children: TreemapNode[] = bins.map((bin) => ({
      name: bin.name,
      children: [],
    }));
    for (const item of items) {
      if (item.value > 0) {
        const index = Math.max(
          0,
          Math.min(binIndex(item.value), bins.length - 1),
        );
        children[index].children!.push(item);
      }
    }

    const hierarchy = d3
      .hierarchy<TreemapNode>({ name: "", children })
      .sum((d) => d.value ?? 0)
      .sort((a, b) => (b.value ?? 0) - (a.value ?? 0));

    return d3
      .treemap<TreemapNode>()
      .tile(d3.treemapSquarify)
      .size([dimensions.width, dimensions.height])
      .padding(3)
      .round(true)(hierarchy);
  }, [items, bins, binIndex, dimensions]);

  const binClass = React.useMemo(
    () => new Map(bins.map((bin) => [bin.name, bin.className])),
    [bins],
  );

  return (
    <Tooltip.Provider>
      <svg
        viewBox={`0 0 ${dimensions.width} ${dimensions.height}`}
        width="100%"
        height="100%"
      >
        {root.leaves().map((leaf, index) => {
          const label = format(leaf.data.value ?? 0);
          const rectId = `${id}-rect-${index}`;
          const itemId = leaf.data.id ?? null;

          return (
            <Tooltip.Root delayDuration={0} key={index}>
              <g
                transform={`translate(${leaf.x0}, ${leaf.y0})`}
                opacity={highlight === null || highlight === itemId ? 1 : 0.75}
                className="cursor-pointer"
                onClick={() => onHighlight(itemId)}
                onContextMenu={(event) => {
                  if (itemId !== null) {
                    onItemContextMenu(event, itemId);
                  }
                }}
              >
                <Tooltip.Trigger asChild>
                  <rect
                    id={rectId}
                    x="0"
                    y="0"
                    width={leaf.x1 - leaf.x0}
                    height={leaf.y1 - leaf.y0}
                    className={binClass.get(leaf.parent?.data.name ?? "")}
                    strokeWidth={highlight === itemId ? 3 : 1}
                  />
                </Tooltip.Trigger>
                {leaf.x1 - leaf.x0 > 25 && leaf.y1 - leaf.y0 > 25 ? (
                  <>
                    <clipPath id={`${id}-clip-${index}`}>
                      <use href={`#${rectId}`} xlinkHref={`#${rectId}`}></use>
                    </clipPath>
                    <text
                      className="fill-ps-secondary text-tiny"
                      clipPath={`url(#${id}-clip-${index})`}
                    >
                      <tspan x="8" y="20">
                        {leaf.data.name}
                      </tspan>
                      <tspan x="8" dy="1.5em" className="font-semibold">
                        {label}
                      </tspan>
                    </text>
                  </>
                ) : null}
              </g>
              <Tooltip.Portal>
                <Tooltip.Content
                  className="bg-ps-primary text-ps-text-primary text-tiny rounded-xs px-2 py-1"
                  sideOffset={5}
                >
                  <p>{leaf.data.name}</p>
                  <p className="font-semibold">{label}</p>
                  <Tooltip.Arrow className="fill-ps-primary" />
                </Tooltip.Content>
              </Tooltip.Portal>
            </Tooltip.Root>
          );
        })}
      </svg>
    </Tooltip.Provider>
  );
}

export default Treemap;
