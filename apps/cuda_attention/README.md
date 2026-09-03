# cuda_attention

Single-head attention on the tensor cores, `softmax(Q K^T / sqrt(depth)) V`,
with fp16 inputs, fp32 accumulation and softmax arithmetic, and an fp16
output. That is the function, the precisions and the store type of torch's
`scaled_dot_product_attention` at its defaults (no mask, no dropout), so every
row the runner prints computes the same thing torch's kernels do.

The benchmark shape is the one `apps/inductive_benchmarks.py` builds: 65536
queries, 1024 keys, depth 64, one head, batch 1.

## The Halide forms

Both flash forms walk the keys in chunks (`chunk` keys per step, 64 by
default), keep one chunk's scores in tensor core fragments, and carry an
online softmax across the walk: the running row maximum, the running row sum,
and the output accumulator, rescaled whenever the maximum moves. The softmax
scale is folded into the exponent, so the first multiply's operands are the
fp16 inputs as given, and the output is narrowed to fp16 in a fragment and
stored from there. They differ in how the carried state is written:

- **`attention_flash`** (inductive). The running maximum and row sum are
  inductive Funcs - each step defined in terms of the step before, with the
  first step given separately - and the accumulator's update reads its own
  previous value. The compiler sees that only the last step is live and folds
  the state to two tiles; the walk is unrolled by two so that which of the two
  a step reads is a constant. Register cap: `gpu_max_registers(128)`, fixed in
  the generator.

- **`attention_flash_rdom`** (RDom only). The same online softmax with no
  inductive Funcs: the maximum, row sum and accumulator are one Tuple at the
  accumulator's shape, the two per-row values broadcast across the columns,
  advanced by a single update over the key chunks. Each chunk's weights are
  taken against the chunk's own maximum, so its row sum and product with V
  are reductions of their own, and the update folds them in with the two
  rescalings. Register cap: the `max_registers` generator param, set from the
  Makefile's `RDOM_MAX_REGS`; its chunk is `RDOM_CHUNK`. Both are tuned
  separately from the inductive form's, since the three-component state wants
  more registers than the two-component one.

The runner also builds **`attention`**, a fused form that holds every key's
scores in registers at once with no walk. It only launches up to 128 keys, so
at the benchmark shape the runner skips it and says so.

## Baselines

- **torch SDPA** (`torch_bench.py`): the FlashAttention-2, cuDNN and
  memory-efficient backends, each timed on its own through `sdpa_kernel`, at
  the same shape, dtype and default scale. The script prints the torch
  version and one row per backend.
- **cuBLAS + softmax + cuBLAS** (in the runner): the same attention unfused.
  cuBLAS multiplies into an fp32 scores matrix in global memory with the
  softmax scale as its alpha, the `attention_softmax` filter (also on tensor
  cores) normalises it into fp16, and cuBLAS multiplies again with an fp16
  store. The runner also prints where its time goes, phase by phase.

All rows use the shared timing protocol of `apps/support/bench_harness.h`
(`HB_WARMUP`, `HB_TRIALS`, `HB_BATCH`; best trial reported), and
`torch_bench.py` follows the same protocol with CUDA events.

## Input contract

Q is `depth x queries`, K is `depth x keys`, V is `out_depth x keys`, all
fp16 and dense along the first dimension (each query's or key's vector is
contiguous). The runner fills them with standard normal samples from a fixed
seed, so every run sees the same realistic values.

**`attention_flash` takes padded K and V.** Its walk warms up by rewinding
two chunks before the first key, and rather than clamping the index it reads
them, so the caller must hand it K and V with `2 * chunk` extra rows in front
of row 0 (their `dim(1)` min is `-2 * chunk`). What is in there only has to
be finite: the first step rescales an accumulator that is still zero. The
runner keeps separate zero-filled copies for it; the other two Halide forms
and the cuBLAS path take K and V unpadded.

## Reference check

The runner checks every row's output against a double precision softmax on
the host over a sample of query rows, and prints the worst error as a
fraction of the tolerance. The tolerance is what the forms' own rounding
allows for: the fp16 rounding of the output (2^-11 of the value), plus eight
standard deviations of the noise from the weights passing through an fp16
operand into the second multiply (which torch's kernels also do). Set
`HL_SKIP_CHECK` to skip it.

## Where the numbers are

Timings live in the table `apps/inductive_benchmarks.py` prints, with the raw
output under `apps/inductive_benchmarks_logs/flash_attention*.txt`. The
driver rebuilds this app from clean at the benchmark shape, runs the runner
and `torch_bench.py`, and reports the inductive time, the RDom time and the
fastest baseline. The tables in the comments of `attention_generator.cpp`
are from earlier shapes and precisions; the driver's table is the measurement.
