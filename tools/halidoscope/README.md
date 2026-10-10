# Halidoscope

An interactive GUI and CLI for working with Halide traces.

## Prerequisites

You'll need a few prerequisites to get everything working.

1. Tauri's
   [system dependencies](https://v2.tauri.app/start/prerequisites/#system-dependencies)
   for your OS.
   - Note that you only need dependencies for desktop targets.
2. A [Rust](https://rust-lang.org/learn/get-started/) installation.
3. A [Node.js](https://nodejs.org/en/download) installation.
4. [PNPM](https://pnpm.io/), a space-efficient package manager for the
   JavaScript ecosystem.

## Building Halidoscope

To get a production build locally, run the following two commands:

```bash
pnpm install
pnpm tauri build
```

This will write the Halidoscope executable to
`tools/halidoscope/src-tauri/target/release/halidoscope`. You can, of course,
symlink this executable to any directory on your `PATH`. On Unix systems:

```bash
ln -sf tools/halidoscope/src-tauri/target/release/halidoscope /some/dir/on/your/path/halidoscope
```

## Using Halidoscope

### Calling Halidoscope from a Halide program

Halide exposes a member function on the `Pipeline` class,
`Pipeline::halidoscope`, that allows you to launch Halidoscope directly from an
executing program. This call will execute the your pipeline once with tracing
enabled, once with profiling enabled, and then launch an interactive Halidoscope
session.

```cpp
// Normal algorithm definition and scheduling code.

// Create the pipeline.
Pipeline pipeline(output);

// Example call where we explicitly pass the size of output buffer we want
// Halidoscope to allocate. In this case, we want to match to the dimensions of
// our input buffer exactly.
std::vector<int32_t> sizes = {input.width(), input.height(), input.channels()};
pipeline.halidoscope(sizes);
```

The first argument to `Pipeline::halidoscope` matches that of
`Pipeline::realize` and can be one of:

1. A `sizes` `std::vector<int32_t>` defining the dimensionality of output
   buffers for Halidoscope (and, under the hood, the Halide runtime) to
   allocate.
2. An `output` `RealizationArg` representing an already-allocated destination
   (e.g., a `Buffer`) for Halidoscope to write to.

As with `Pipeline::realize`, either may be preceded by a `JITUserContext *`.

#### Calling Halidoscope from a Generator

Generators have a protected `halidoscope` member function that can be called
from `generate()` once all outputs are defined. It takes the same arguments as
the `Callable` returned by `compile_to_callable`: an optional
`JITUserContext *`, then the inputs in order, then the output buffers. The
inputs are bound to the Generator's input parameters and `get_pipeline()` is
passed to `Pipeline::halidoscope`. A `HalidoscopeOptions` may be passed as the
very first argument.

```cpp
void generate() {
    // Define and schedule the outputs.

    Buffer<float> input_buffer = Halide::Tools::load_and_convert_image("input.png");
    Buffer<float> output_buffer(input_buffer.width(), input_buffer.height());
    halidoscope(input_buffer, 0.5f, output_buffer);
}
```

#### Configuring Halidoscope's behavior

The `Pipeline::halidoscope` API also accepts an `options` argument of type
`HalidoscopeOptions` that can control how Halidoscope behaves. This struct has
the following shape:

| Field                   | Type                         | Description                                                                                                                                                                                                                                                              |
| ----------------------- | ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `path`                  | `std::optional<std::string>` | The path to the Halidoscope binary on disk. By default, `Pipeline::halidoscope` will look for `halidoscope` on the user's `$PATH` and error if not found.                                                                                                                |
| `output_dir`            | `std::optional<std::string>` | (Optional.) A path to a non-volatile directory for storing Halidoscope-generated trace binaries and profiler output. By default, Halidoscope will write recorded `.hltrace` and profile JSON files to a temporary directory that is destroyed on process exit.           |
| `profile_runs`          | `std::optional<int>`         | The number of profiling runs for the Halide profiler to execute on the pipeline. If unset, runs the pipeline repeatedly for at least one second (not counting the first run, which includes JIT compilation). Users can opt out of profiling altogether by specifying 0. |
| `trace_file_size_limit` | `size_t`                     | A limit on the size of the compressed trace file, in megabytes (default 1000). If the trace exceeds it, `Pipeline::halidoscope` fails with an error rather than filling the disk.                                                                                        |

### Calling Halidoscope from the command line

As an alternative to the `Pipeline::halidoscope` API, you can also invoke
Halidoscope directly from the command line to launch the GUI. To work with a
pre-recorded trace, simply specify the path to a Halide trace binary file via
the `--trace` flag. Traces may be raw (e.g. as written by `HL_TRACE_FILE`) or
compressed (as written by `Pipeline::halidoscope`, using
`tools/halide_trace_compression.h`).

```bash
halidoscope --trace <path/to/file.hltrace>
```

If you'd also like to visualize a pre-recorded profile JSON file, pass the path
to that file via the `--profile` flag. Note that `--trace` is always required.

```bash
halidoscope --trace <path/to/file.hltrace> --profile <path/to/profile.json>
```

### Additional CLI commands

`halidoscope` also exposes a non-interactive CLI for gathering information about
your Halide pipeline.

#### `list`

List the `Func`s in a trace, along with their dimensionality.

```bash
halidoscope list --trace <path/to/file.hltrace> [--json]
```

- `-t, --trace <path>` (required): Path to the `.hltrace` file to analyze.
- `--json`: Print output as JSON instead of a table.

#### `stats`

Print statistics (minimum/maximum coordinates, minimum/maximum value, maximum
store/load counts, and thread count) for one or all `Func`s in a trace.

```bash
halidoscope stats --trace <path/to/file.hltrace> [--func <name>] [--json]
```

- `-t, --trace <path>` (required): Path to the `.hltrace` file to analyze.
- `-f, --func <name>`: Name of the Func to print statistics for. If omitted,
  prints statistics for all Funcs.
- `--json`: Print output as JSON instead of a table.

#### `dot`

Generate a [Graphviz DOT](https://graphviz.org/doc/info/lang.html)
representation of the pipeline's dataflow graph.

```bash
halidoscope dot --trace <path/to/file.hltrace> [destination]
```

- `-t, --trace <path>` (required): Path to the `.hltrace` file to analyze.
- `destination` (optional): Path to write the DOT file. Must end in `.txt`,
  `.gv`, or `.dot`. If omitted, prints the DOT source to stdout.

#### `snapshot`

Snapshot a `Func`'s values at a given packet index for a given render mode,
writing the underlying data to a JSON file.

```bash
halidoscope snapshot --trace <path/to/file.hltrace> --func <name> [--packet-index <n>] [--mode <mode>] <destination>
```

- `-t, --trace <path>` (required): Path to the `.hltrace` file to snapshot.
- `-f, --func <name>` (required): Name of the Func to snapshot.
- `-i, --packet-index <n>`: Global packet index to snapshot. Defaults to `0`.
- `-m, --mode <mode>`: Rendering mode. One of `grayscale` (default), `rgb`
  `store-frequency`, `load-frequency`, `redundant-stores`, or `reuse-distance`.
- `destination` (required): Path to write the output snapshot. Must end in
  `.json`.

## Developing Halidoscope

Developing Halidoscope locally should be fairly straightforward. Assuming you've
installed the [prequisities](#prerequisites), just run the following two
commands.

```bash
pnpm install
pnpm tauri dev -- -- --trace <path/to/file.hltrace> [--profile <path/to/profile.json>]
```

These commands will install all necessary JavaScript and Rust dependencies,
build the Rust backend (using the `dev` profile), and start Vite's dev server.
Changes on both the Rust and TypeScript sides will trigger automatic rebuilds
with hot reloading — no need to stop your dev server while developing!
