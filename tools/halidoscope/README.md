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

```sh
pnpm install
pnpm tauri build
```

This will write the Halidoscope executable to
`<thisDir>/src-tauri/target/release/halidoscope`. You can, of course, symlink
this executable to any directory on your `PATH`. On Unix systems:

```sh
ln -sf /path/to/thisDir/src-tauri/target/release/halidoscope /some/dir/on/your/path/halidoscope
```

## Using Halidoscope

### Running the GUI

To run the GUI, pass a Halide trace binary file to `halidoscope` via the
`--trace` flag.

```sh
halidoscope --trace <path/to/file.hltrace>
```

This will load the specified trace file and start up the GUI.

### Using the CLI

`halidoscope` also exposes a CLI for gathering information about your Halide
pipeline.

#### `list`

List the `Func`s in a trace, along with their dimensionality.

```sh
halidoscope list --trace <path/to/file.hltrace> [--json]
```

- `-t, --trace <path>` (required): Path to the `.hltrace` file to analyze.
- `--json`: Print output as JSON instead of a table.

#### `stats`

Print statistics (minimum/maximum coordinates, minimum/maximum value, maximum
store/load counts, and thread count) for one or all `Func`s in a trace.

```sh
halidoscope stats --trace <path/to/file.hltrace> [--func <name>] [--json]
```

- `-t, --trace <path>` (required): Path to the `.hltrace` file to analyze.
- `-f, --func <name>`: Name of the Func to print statistics for. If omitted,
  prints statistics for all Funcs.
- `--json`: Print output as JSON instead of a table.

#### `dot`

Generate a [Graphviz DOT](https://graphviz.org/doc/info/lang.html)
representation of the pipeline's dataflow graph.

```sh
halidoscope dot --trace <path/to/file.hltrace> [destination]
```

- `-t, --trace <path>` (required): Path to the `.hltrace` file to analyze.
- `destination` (optional): Path to write the DOT file. Must end in `.txt`,
  `.gv`, or `.dot`. If omitted, prints the DOT source to stdout.

#### `snapshot`

Snapshot a `Func`'s values at a given packet index for a given rendering mode,
writing the underlying data to a JSON file.

```sh
halidoscope snapshot --trace <path/to/file.hltrace> --func <name> [--packet-index <n>] [--mode <mode>] <destination>
```

- `-t, --trace <path>` (required): Path to the `.hltrace` file to snapshot.
- `-f, --func <name>` (required): Name of the Func to snapshot.
- `-i, --packet-index <n>`: Global packet index to snapshot. Defaults to `0`.
- `-m, --mode <mode>`: Rendering mode. One of `grayscale` (default), `rgb`,
  `store-frequency`, `load-frequency`, `redundant-stores`, or `reuse-distance`.
- `destination` (required): Path to write the output snapshot. Must end in
  `.json`.

## Developing Halidoscope

To develop Halidoscope locally, run the following two commands:

```sh
pnpm install
pnpm tauri dev -- -- --trace <path/to/file.hltrace>
```
