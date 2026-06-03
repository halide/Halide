# Halidoscope

(Another) Interactive trace visualizer for Halide.

## Prerequisites

You'll need a few prerequisites (in addition to the usual Halide development
setup) to get everything working.

1. A [Rust](https://rust-lang.org/learn/get-started/) installation.
2. A [Node.js](https://nodejs.org/en/download) installation.
3. [PNPM](https://pnpm.io/), a space-efficient package manager for the
   JavaScript ecosystem.

> You can likely get away with using NPM directly, but `npm install` will not
> respect the version ranges in `pnpm-lock.yaml`.

## Development

### Backend

```sh
uv sync --no-install-project halide
```

### Frontend

```sh
pnpm install
```

## Starting Things Up

1. Run the backend locally.

```sh
cd backend
uv run dev
```

2. Run the frontend, pointing it at a Halide trace.

```sh
cd frontend
pnpm tauri dev -- -- --trace <your_trace>
```

This should launch Halidoscope in development mode.
