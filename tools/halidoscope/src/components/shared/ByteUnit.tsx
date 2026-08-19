import { clsx } from "clsx";

import { formatBytes } from "@/utils/formatters";

interface Props {
  value: number;
  className?: string;
}

function ByteString({ value, className }: Props) {
  const { value: byteValue, unit } = formatBytes(value);

  return (
    <p className={clsx("flex font-semibold", className)}>
      <span>{byteValue}</span>
      <span>&thinsp;</span>
      <span
        className={clsx({
          "text-oxide-purple": unit === "B",
          "text-oxide-green": unit === "KB",
          "text-oxide-yellow": unit === "MB",
          "text-oxide-red": unit === "GB",
        })}
      >
        {unit}
      </span>
    </p>
  );
}

export default ByteString;
