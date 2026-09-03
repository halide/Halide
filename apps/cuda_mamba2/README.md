# Mamba-2 SSD scan on tensor cores

The chunked SSD recurrence of Mamba-2, forward and backward, with the
state carried between chunks either as an inductive Func (slid into the
kernels that consume it) or as an update definition over an RDom (a
materialized walk, written out at every chunk).

- `mamba2_generator.cpp`: the forward. `scan=inductive`, `scan=rdom`
  (the walk over a zero-filled state) and `scan=rdom_undef` (the walk
  over an undefined pure definition, with the base case as its first
  update: the fewest kernels an RDom form can have). `wmma=true` puts
  the four chunk multiplies on the tensor cores.
- `mamba2_bwd_generator.cpp`: the backward, with the forward's state
  recomputed and the gradient state walked the other way. The step-size
  and decay gradients follow mamba_ssm's stable path: sums over the
  position pairs each log-decay increment sits between, gathered per
  chunk on the tensor cores, rather than the suffix-summed adjoint
  identity, which cancels catastrophically in float.
- `runner.cpp`, `runner_bwd.cpp`: build the inputs, check every output
  against a serial double-precision reference, and time the pipeline
  with the shared harness (ten launches per synchronization).
- `triton_bench.py`: mamba_ssm's Triton kernels
  (`mamba_chunk_scan_combined` and its backward) at the same shape,
  timed with CUDA events under the same protocol.

`make test` and `make test_bwd` take `SEQ STATE CHANNELS HEADS GROUPS
CHUNK SCAN WMMA` on the command line. The numbers in the paper's table
come from `apps/inductive_benchmarks.py`, which builds each side at its
own best chunk (Triton 256; the Halide backward 128, where its causal
pruning makes smaller chunks cheaper) and lists the shape in the row.

Tuning asymmetries, all measured: the group score-gradient kernel is
capped at 168 registers in both forms (`gpu_max_registers`), because
left alone the RDom build's takes 255 and halves its occupancy; the
inductive forward prefetches its operand tiles a chunk ahead, which the
RDom form cannot, since its walk owns the loop.
