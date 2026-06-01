"""Profile trace loading to identify bottlenecks."""

import cProfile
import pstats
import sys
from pathlib import Path


def main():
    if len(sys.argv) < 2:
        print("Usage: python profile_load.py <trace_file.bin>")
        sys.exit(1)

    trace_path = Path(sys.argv[1])
    if not trace_path.exists():
        print(f"File not found: {trace_path}")
        sys.exit(1)

    print(
        f"Profiling load of {trace_path} "
        f"({trace_path.stat().st_size / 1024 / 1024:.1f} MB)"
    )
    print()

    from neotrace.trace import Trace

    # Profile the load
    profiler = cProfile.Profile()
    profiler.enable()

    trace = Trace.load(str(trace_path))

    profiler.disable()

    print(f"Loaded {len(trace)} packets, {len(trace.funcs)} funcs")
    print()

    # Show stats sorted by cumulative time
    stats = pstats.Stats(profiler)
    stats.strip_dirs()

    print("=" * 70)
    print("Top 30 functions by cumulative time:")
    print("=" * 70)
    stats.sort_stats("cumulative").print_stats(30)

    print("=" * 70)
    print("Top 30 functions by total time (self, excluding subcalls):")
    print("=" * 70)
    stats.sort_stats("tottime").print_stats(30)


if __name__ == "__main__":
    main()
