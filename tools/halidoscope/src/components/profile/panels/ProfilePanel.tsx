import { clsx } from "clsx";

interface Props {
  label: string;
  className?: string;
  contentClassName?: string;
}

function ProfilePanel({
  label,
  className = "",
  contentClassName = "items-center justify-center p-4",
  children,
}: React.PropsWithChildren<Props>) {
  return (
    <div
      className={clsx(
        "bg-ps-secondary border-ps-titlebar flex min-h-0 basis-1/2 flex-col border-x-3 border-t-0 text-xs last:border-l-0",
        className,
      )}
    >
      <div className="bg-ps-titlebar border-ps-border-primary flex border-r">
        <span className="bg-ps-primary text-ps-text-primary border-ps-border-primary border px-3 py-1 font-semibold">
          {label}
        </span>
      </div>
      <div className={clsx("flex min-h-0 flex-1", contentClassName)}>
        {children}
      </div>
    </div>
  );
}

export default ProfilePanel;
