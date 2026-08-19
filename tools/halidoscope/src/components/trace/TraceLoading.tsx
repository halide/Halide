import { motion } from "motion/react";

interface Props {
  message: string;
  progress: number;
}

function TraceLoading({ message, progress }: Props) {
  return (
    <div className="bg-ps-secondary flex h-full flex-col items-center justify-center gap-4 text-white">
      <p>{message}</p>
      <div className="border-ps-border-tertiary bg-ps-text-primary relative flex h-4 w-80 items-center rounded-sm px-px">
        <motion.div
          className="bg-ps-border-primary h-3.5 rounded"
          animate={{ width: `${progress}%` }}
        ></motion.div>
      </div>
    </div>
  );
}

export default TraceLoading;
