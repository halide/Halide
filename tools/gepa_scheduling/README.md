# GEPA scheduling-skill experiment

This standalone tool evaluates and optimizes Halide's scheduling skill with
[GEPA](https://gepa-ai.github.io/gepa/). It is intentionally separate from
Halide's primary Python workspace and is not run in CI: real evaluations invoke
GitHub Copilot CLI and consume AI credits.

The candidate has nine components: `SKILL.md`, the directive cheat sheet, and
guide chapters 05 through 11. All other scheduling references remain available
to the agent but immutable. GEPA runs in generalization mode with distinct
train, validation, and held-out test tasks. Validators use exact JSON
invariants, compilation, correctness checks, and normalized `print_loop_nest()`
patterns; no LLM grades another LLM's answer.

## Prerequisites

- Python 3.11 or newer and `uv`
- an authenticated `copilot` CLI
- an explicit Claude Sonnet model available to that Copilot account
- an installed Halide CMake package for code tasks

Copy and edit the example configuration. Pin the exact Copilot model string; do
not use a moving "latest" alias. Set `halide_dir` to the directory containing
the installed package's `HalideConfig.cmake`.

```sh
cd tools/gepa_scheduling
cp config.example.toml config.toml
uv sync --extra dev
```

`config.toml` is local configuration and must not contain credentials. Copilot
uses its existing authentication. Each run records resolved configuration and
the Copilot CLI version, but never environment variables or tokens.

## Safe checks

These commands use no AI credits:

```sh
uv run halide-gepa-scheduling --config config.toml validate
uv run halide-gepa-scheduling --config config.toml smoke
uv run pytest
uv run ruff check .
uv run ruff format --check .
```

The smoke test uses a fake Copilot backend and exercises worktree setup,
candidate installation, deterministic validation, scoring, and cleanup.

## Baselines and optimization

Run the no-skill and current-skill baselines before optimization:

```sh
uv run halide-gepa-scheduling --config config.toml evaluate \
	--split validation --mode none --output runs/baseline-none.json
uv run halide-gepa-scheduling --config config.toml evaluate \
	--split validation --mode current --output runs/baseline-current.json
```

Start the bounded GEPA run:

```sh
uv run halide-gepa-scheduling --config config.toml optimize
```

GEPA state and the best candidate are stored under `runs/gepa/`. Evaluate the
saved candidate on the held-out split before considering promotion:

```sh
uv run halide-gepa-scheduling --config config.toml evaluate \
	--split test --mode candidate --candidate runs/gepa/best_candidate.json \
	--output runs/test-candidate.json
```

Run a paired, repeated held-out comparison and emit a patch only if it passes:

```sh
uv run halide-gepa-scheduling --config config.toml compare \
	--candidate runs/gepa/best_candidate.json --repeats 3 \
	--output runs/comparison.json
uv run halide-gepa-scheduling --config config.toml stage \
	--candidate runs/gepa/best_candidate.json --report runs/comparison.json \
	--output runs/candidate.patch
```

`stage` never overwrites the skill. Review the patch manually. An optimized
candidate must retain every held-out hard correctness check passed by the
current skill. Lower token use or duration is only a tie-breaker after
correctness; training score alone is never sufficient for promotion. Running
`optimize` again with the same `runs/gepa/` directory resumes GEPA's saved
state.

## Security and reproducibility

Rollouts use unique detached git worktrees. Copilot receives only read, write,
and shell tools; URL tools are unavailable, prompts prohibit network access, and
hidden validator expectations remain outside the writable worktree. Timeouts and
captured-output limits are configured. Failed CLI invocations, timeouts,
malformed answers, compiler errors, and integrity violations produce explicit
failed evaluations and diagnostic feedback for GEPA.

Run artifacts can contain source patches and model transcripts. They are ignored
by Git and should be reviewed before sharing.
