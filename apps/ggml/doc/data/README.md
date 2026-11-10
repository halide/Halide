# Benchmark data

Raw `ggml-quant-bench` output (CSV; `.err`: each run's commit, time and
command line, and its check summary). Columns: see `../../README.md`; time is
`ns` (`ns_lo`/`ns_hi`: its 95% CI). Machine: Xeon w5-2455X (Sapphire Rapids,
12 cores, AMX), RTX A4500, WSL2 (`cpu-sweep/machine.txt`).

- `cpu-sweep/`: the current CPU dataset. 4096^2 weights, M = 1..512, 12
  threads and 1, every format and provider but CUDA (`run.sh`; Windows power
  plan High Performance). Summarized in `../X86_CUDA_LOG.md` ("CPU sweep").
- `phase1-final/`: the Phase 1 report's run (`run.sh`; `gemm_t1.csv` one
  thread at M = 32, `gv_all.csv` llama3-8b gemv shapes), Balanced power plan.
- `phase2-cuda/`: the Phase 2 report's CUDA timings (text, per format, M and
  provider; `tablemin.txt` the reported GGML / Halide table).

Scripts:

- `table.py THREADS a.csv ...`: GGML / Halide per format and M, each side's
  fastest CPU path (GGML with f32 activations, which it quantizes itself);
  > 1: Halide faster.
- `analyze.py a.csv ... [--by-m] [--cases]`: the paper's Table 4 metric:
  G/H per case against GGML's fastest CPU path, ours one layout per format
  held out (chosen on the other shapes), split into blocked-dot and
  format-specific strata; >= 0.95 a match.
