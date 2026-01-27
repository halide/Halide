"""
Command-line entry point for neotrace.
"""

from __future__ import annotations

import argparse

import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(
        prog="neotrace",
        description="Interactive trace visualization for Halide",
    )
    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    # view command - interactive viewer
    view_parser = subparsers.add_parser("view", help="Open interactive trace viewer")
    view_parser.add_argument("trace", type=Path, nargs="?", help="Trace file to open")
    view_parser.add_argument("--config", type=Path, help="Layout configuration file")

    # info command - print trace info
    info_parser = subparsers.add_parser("info", help="Print trace information")
    info_parser.add_argument("trace", type=Path, help="Trace file to analyze")
    info_parser.add_argument(
        "--verbose", "-v", action="store_true", help="Show detailed info"
    )
    info_parser.add_argument(
        "--dag", action="store_true", help="Print inferred DAG in DOT format"
    )

    # render command - render to video (future)
    render_parser = subparsers.add_parser("render", help="Render trace to video")
    render_parser.add_argument("trace", type=Path, help="Trace file to render")
    render_parser.add_argument(
        "-o", "--output", type=Path, required=True, help="Output video file"
    )
    render_parser.add_argument("--config", type=Path, help="Layout configuration file")
    render_parser.add_argument(
        "--size", nargs=2, type=int, default=[1920, 1080], help="Output size"
    )
    render_parser.add_argument("--fps", type=int, default=30, help="Frames per second")

    args = parser.parse_args()

    if args.command is None:
        # Default to view if no command specified
        args.command = "view"
        args.trace = None
        args.config = None

    if args.command == "view":
        from .viewer import run_viewer

        sys.exit(run_viewer(args.trace))

    elif args.command == "info":
        from tqdm import tqdm

        from halide import Trace

        last_bytes = 0
        with tqdm(unit="B", unit_scale=True, unit_divisor=1024) as pbar:
            pbar.set_description("Loading trace")

            def update_progress(bytes_read, total_bytes):
                nonlocal last_bytes
                pbar.total = total_bytes
                pbar.update(bytes_read - last_bytes)
                last_bytes = bytes_read

            trace = Trace.load(str(args.trace), update_progress)

        if args.dag:
            print(trace.dag_as_dot())
        else:
            print(f"Trace: {len(trace)} packets, {len(trace.funcs)} funcs")
            print(f"Pipelines: {len(trace.pipelines)}")
            if args.verbose:
                for name, stats in sorted(trace.funcs.items()):
                    coords = ""
                    if stats.min_coords and stats.max_coords:
                        extents = [
                            f"[{lo}, {hi})"
                            for lo, hi in zip(stats.min_coords, stats.max_coords)
                        ]
                        coords = " x ".join(extents)
                    print(f"  {name}: {coords}")

    elif args.command == "render":
        print("Render command not yet implemented.")
        print("For now, use the interactive viewer to set up your layout,")
        print("export the config, and stay tuned for video rendering.")
        sys.exit(1)


if __name__ == "__main__":
    main()
