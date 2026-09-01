Batched global alignment of sequence pairs with affine gap costs
(Needleman-Wunsch / Gotoh): the canonical two-dimensional dynamic
program, filling a table that looks up, left, and diagonally. The DP
state is a Tuple (H, E, F) of int16 score planes, INDUCTIVE IN BOTH
table dimensions - the same-row reference H(j-1, i) is what forces the
second dimension inductive, and every reference is non-increasing in
each dim. This is the first app where multi-variable induction is
load-bearing rather than decorative: the diagonal dependency cannot be
separated into chained one-dimensional scans.

The consumer wants five bits per cell - ksw2's traceback direction
byte - not the three 16-bit score planes. That asymmetry is the whole
benchmark:

- scan=inductive: the score rows live in a two-row folded window in
  cache; only direction bytes reach memory.
- scan=unfolded: the same fused walk with folding disabled.
- scan=rdom: the walk as update definitions, which own their axes, so
  all three score planes materialize at full extent per pair before
  the directions can be derived. Nothing smaller is expressible: Tuple
  components share the Func's extent, so the pointers cannot be kept
  while the scores roll.

The baseline is ksw2 (MIT, vendored subset), the affine-gap kernel
family inside minimap2. The recurrence, boundary values, tie-breaking,
and direction-byte encoding mirror ksw_gg exactly, and the runner
ports ksw_backtrack, so the output is BYTE-IDENTICAL: every CIGAR and
score is checked against ksw2's on every run. ksw_gg2_sse (the int8
anti-diagonal production kernel) is the performance baseline; under
PAR=true it gets the same cores, parallelized across pairs the way
aligners deploy it. Random (low-identity) pairs put the comparison in
the dense-DP regime; on high-identity pairs wavefront methods (WFA)
are the right algorithm and would win - cite them as context, not as
this benchmark's competitor.

Measured on a Threadripper 9970X (Zen 5):

    256x256, batch 1024, one thread:
      inductive     13.1 ms   (5.1 Gcell/s)
      unfolded      17.5 ms   (1.34x)
      rdom          21.2 ms   (1.62x)
      ksw2 sse      38.2 ms   (2.92x, same output)
      ksw2 scalar  197   ms   (15x, same output)

    1024x1024, batch 128, one thread:
      inductive     24.4 ms   (5.5 Gcell/s)
      unfolded      39.6 ms   (1.62x)
      rdom          79.1 ms   (3.24x)
      ksw2 sse      67.3 ms   (2.76x, same output)

    1024x1024, batch 4096, all cores (PAR=true), ksw2 threaded:
      inductive     47.7 ms   (90 Gcell/s)
      unfolded     486   ms   (10.2x)
      rdom        1077   ms   (22.6x)
      ksw2 sse      63.0 ms   (1.32x, same output)

Vector-width fairness: ksw2's kernels are 128-bit SSE intrinsics (16
int8 lanes; lh3 ships no wider port), while Halide compiles to
AVX-512. At MATCHED width (HL_TARGET=x86-64-linux-sse41, 1k single
thread) ksw2 wins 1.35x (56.8 ms vs our 76.4): its int8 anti-diagonal
difference formulation is denser than our int16 absolute-score table,
worth 2x in lanes. The advantage this benchmark claims is therefore
not the inner loop - it is that widening is a recompile for the
schedule and a porting project for the intrinsics (an AVX-512 ksw2 is
exactly what Intel's mm2-fast contributed, published as its own
engineering effort; 512-bit lane-crossing shuffles do not translate
mechanically). At full parallelism both sides sit near the memory
wall, where formulation density fades and the remaining edge is the
streaming stores. An int8 difference formulation is expressible as an
inductive Func too and would compound with the width - left undone to
keep the algorithm the textbook recurrence.

The separations grow with scale because they are memory structure: the
rdom form's materialized score slabs are L3-resident in the small
config (ties looming - the chebyshev lesson) and DRAM-bound at
long-read lengths and full parallelism. At 90 Gcell/s the inductive
form is writing direction bytes at the streaming-store wall.

Schedule notes, in the order they mattered:
- Sequences must be BATCH-MAJOR (qseq(b, j)): pair-major layout turns
  every cell's character pair into a 32-lane strided gather, worth 4x.
- A parallel block owns 64 pairs - one full cache line of the
  direction plane - so no two tasks write the same line. False sharing
  on 32-wide blocks cost 4x in parallel and nothing single-threaded.
  The two interleaved vector chains per cell also buy ILP one thread.
- The score rows are computed a row at a time (compute_at the i loop)
  and folded to two; per-cell producer granularity cost 2x.
- dir.stream_stores() under PAR skips read-for-ownership on the plane
  that is written once and read O(N): half the DRAM demand, worth
  1.9x at full parallelism.
- The runner needs the reusing allocator plus mallopt(M_MMAP_THRESHOLD)
  - both ksw2's per-call megabyte workspaces and the rdom form's
  gigabyte slabs otherwise make it a page-fault benchmark.

Knobs: QLEN, TLEN, BATCH, PAR (rebuild with make clean between
changes; PAR also threads the ksw2 baseline).
