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
- scan=diff8: the Suzuki-Kasahara difference formulation ksw2's SIMD
  kernel uses, as an inductive Func: state (U, V, X, Y) of H-deltas
  bounded by the scoring parameters, so it is int8 - twice the lanes.
  The references are only up and left (the diagonal rides in the
  differences), still inductive in both dims, and the direction bytes
  mirror ksw_gg2's, so it stays byte-exact. ~30 declarative lines.

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
      diff8          8.3 ms   (8.1 Gcell/s)
      inductive      9.3 ms   (7.2 Gcell/s)
      unfolded      15.1 ms   (1.62x of inductive)
      rdom          18.9 ms   (2.04x)
      ksw2 sse      38.2 ms   (4.12x, same output)
      parasail      41.2 ms   (4.4x, same scores)
      ksw2 scalar  199   ms   (21x, same output)

    1024x1024, batch 128, one thread - the same generators compiled
    for the baseline's ISA and for the machine's (HL_TARGET=...-sse41
    vs host), against the same 128-bit ksw2 binary. The ksw2
    comparisons are interleaved single-shot medians (INTERLEAVE=15),
    so both sides share thermal and clock state:
      diff8     @ sse4.1    30.5 ms   (matched ISA: 2.22x ahead)
      diff8     @ avx-512   14.9 ms   (a recompile: 4.6x ahead of ksw2)
      inductive @ avx-512   17.4 ms   (3.9x ahead)
      unfolded  @ avx-512   38.9 ms   (2.24x of inductive)
      rdom      @ avx-512   77.4 ms   (4.45x)
      ksw2 sse (128-bit)    68.2 ms   (same output, hand-vectorized)
      parasail striped_16   50.9 ms   (same scores; absolute-score
                                       formulation, like our inductive)

    1024x1024, batch 4096, all cores (PAR=true), ksw2 threaded:
      diff8         41.7 ms   (103 Gcell/s)
      inductive     43.3 ms   (99 Gcell/s)
      unfolded     478   ms   (11.0x of inductive)
      rdom        1067   ms   (24.6x)
      ksw2 sse      59.4 ms   (1.37x, same output)
      parasail      45.5 ms   (1.09x, same scores)

parasail (optional; auto-detected from $(PARASAIL_DIR)) is the second
baseline, and it pairs off against the other scan form: its traceback
variants use Farrar's striped approach with ABSOLUTE scores - the same
formulation as our scan=inductive, with the same strategy the schedule
derives (score rows rolling, per-cell trace bytes as the only
materialization). It cannot use int8 the way ksw2 does: absolute
scores grow with length (2048 at 1k pairs), which is exactly the
overflow the Suzuki-Kasahara differences avoid. parasail is verified
score-identical on every pair; its CIGARs can differ by tie-breaking
but all re-score to the optimum, so ksw2 remains the byte-exact
baseline. At the parallel wall threaded parasail overtakes threaded
ksw2 - formulation density stops mattering when direction-byte
bandwidth is the wall.

Vector-width and formulation fairness: ksw2's kernels are 128-bit SSE
intrinsics (16 int8 lanes; lh3 ships no wider port), while Halide
compiles to AVX-512, and ksw2's kernel also uses a denser formulation
(int8 differences) than the textbook int16 table. The scan forms and
targets separate those factors. At matched ISA (diff8 @ sse4.1,
interleaved medians) the batch formulation is 2.22x ahead: every
instruction is still a 16-lane SSE op, but the schedule keeps four
independent stripes of pairs in flight per cell, filling the latency
of the serial per-cell chain - the ILP ksw2's anti-diagonal gets from
independent inner iterations, manufactured from the batch instead,
without the per-diagonal score-profile fills, byte-carry shifts, and
boundary bookkeeping the rotated iteration pays. (A single 16-wide
stripe is latency-bound and 4.5 percent BEHIND ksw2 - the vectorize
width is the schedule's load-bearing choice.) The scope condition to
state: inter-sequence SIMD requires a batch of comparable-length
pairs, the deployment shape of an aligner's scoring stage; ksw2
aligns one pair at a time. And the recompile to AVX-512 is worth a
further 2x, which for the intrinsics kernel
is a porting project, not a flag (an AVX-512 ksw2 is what Intel's
mm2-fast contributed, published as its own engineering effort;
512-bit lane-crossing shuffles do not translate mechanically). At
full parallelism all forms sit near the memory wall, where the
remaining edge is the streaming stores.

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
