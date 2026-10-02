import { open } from "@tauri-apps/plugin-dialog";

interface Props {
  onUpload: (path: string) => void;
}

function TraceUpload({ onUpload }: Props) {
  async function handleClick() {
    const path = await open({
      multiple: false,
      filters: [{ name: "Halide Trace", extensions: ["hltrace"] }],
    });

    if (!path) {
      return;
    }

    onUpload(path);
  }

  return (
    <div className="bg-ps-secondary flex h-full items-center justify-center text-white">
      <button
        type="button"
        onClick={handleClick}
        className="border-ps-border-tertiary hover:bg-ps-border-primary/20 flex h-64 w-96 cursor-pointer flex-col items-center justify-center gap-2 rounded border-2 border-dashed transition-colors"
      >
        <span className="text-ps-text-primary font-semibold">
          Upload a trace
        </span>
        <span className="text-ps-text-secondary text-sm">
          Click to select a .hltrace file
        </span>
      </button>
    </div>
  );
}

export default TraceUpload;
