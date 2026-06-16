function HandleCircle({ zoom }: { zoom: number }) {
  return (
    <svg
      viewBox="0 0 20 20"
      width="20"
      height="20"
      className="absolute left-1/2 top-1/2 pointer-events-none"
      style={{
        transform: `translate(-50%, -50%) scale(clamp(0.25, ${1 / zoom}, 1))`,
      }}
    >
      <circle cx="10" cy="10" r="8" className="fill-ps-text-primary" />
    </svg>
  );
}

export default HandleCircle;
