import { useViewport } from "@xyflow/react";

function HandleCircle() {
  const { zoom } = useViewport();

  return (
    <svg
      viewBox="0 0 20 20"
      width="20"
      height="20"
      className="pointer-events-none absolute top-1/2 left-1/2"
      style={{
        transform: `translate(-50%, -50%) scale(clamp(0.25, ${0.5 / zoom}, 1))`,
      }}
    >
      <circle cx="10" cy="10" r="8" className="fill-ps-text-primary" />
    </svg>
  );
}

export default HandleCircle;
