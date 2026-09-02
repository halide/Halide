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

Two generators express the two score formulations, each supporting the
same three scan forms:
- align16 - the int16 ABSOLUTE-SCORE table, the textbook (H, E, F)
  recurrence.
- align8 - the int8 DIFFERENCE formulation of Suzuki-Kasahara that
  ksw2's SIMD kernel uses: state (U, V, X, Y) of adjacent-cell H
  differences bounded by the scoring parameters, so the state is eight
  bits - twice the lanes and two-thirds the row bytes. In (j, i)
  coordinates its references are only up and left (the diagonal rides
  in the differences), so it needs no anti-diagonal iteration; still
  inductive in both dims, direction bytes mirror ksw_gg2's.

The three scan forms (scan= on either generator):
- inductive: the score rows live in a two-row folded window in cache;
  only direction bytes reach memory.
- unfolded: the same fused walk with folding disabled - an explicit
  fold factor of the whole table, because bound_storage only inflates
  the allocation while the automatic folding pass still folds the
  indexing (a lesson learned the hard way; see the numbers).
- rdom: the walk as update definitions, which own their axes, so the
  whole score state materializes at full extent per pair before the
  directions can be derived. Nothing smaller is expressible: an update
  definition writes only its own buffer, so the direction byte must be
  a separate Func that reads the materialized scores back - and the
  Halide profiler shows that read-back, not the table fill, is where
  the time goes (~17x the fill cost at 1kx1k).

The whole deliverable is Halide, including the traceback. The
direction plane is an intermediate Func realized one block of 64 pairs
at a time (dir.compute_at(path, bo)), and the traceback is an
inductive Func over the step index - state (i, j, ksw2 state, op) per
pair, every lane walking query+target steps in lockstep with a
data-dependent gather from the plane - consuming each block's plane
while it is the freshest thing in cache. This needed one classifier
refinement (src/Inductive.cpp): a select whose condition reads the
function itself is a data-dependent step, not a base-case guard, so
both of its branches may recurse. The pipeline's output is the op
stream in ksw2's backward order (3 once a pair is done); the runner's
only remaining work is run-length encoding it into a CIGAR, which is
timed and included. Two consequences: the vectorized walk replaces a
16 us/pair scalar pointer chase over a batch-major plane, and the plane
is transient - 64 MB per task instead of a retained 4 GB - which is
the same contract the baselines run (fill, trace, free).

The rdom forms use an RDom walk too (an update definition over the
steps, storage shifted by one so the start state sits at index zero),
so scan=rdom is RDom throughout. The two walks tie - every rdom total
is unchanged within noise after the swap - and that is the expected
result: the walk's consumer wants every step and its per-step state is
six bytes, so there is nothing to fold away and no thin projection to
extract. The inductive form's value is in the fill, where the state is
fat and the wanted projection thin; the walk is the same computation
either way, and the paper should claim exactly that.

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

Measured on a Threadripper 9970X (Zen 5). Every number is the full
contract - fill, traceback, and run-length CIGAR - and the baseline
comparisons are interleaved single-shot medians (INTERLEAVE=9 rounds
cycling int8, int16, ksw2, parasail) so all sides share thermal and
clock state; cold single-shots flatter whichever side runs first by up
to 25 percent.

    1024x1024, batch 128, one thread, interleaved medians:

      generators compiled for:              sse4.1    avx2   avx-512
      int8  inductive (align8)               32.2    20.0     14.9 ms
      int16 inductive (align16)              46.3    22.7     16.9 ms
      ksw2 gg2_sse    (128-bit, int8 diffs)  67.7    67.5     67.3 ms
      parasail striped_16 (avx2, int16)      48.8    51.1     49.4 ms

    Like-for-like cells: ksw2 vs int8 at its ISA and formulation
    (sse4.1) 2.1x; parasail vs int16 at its ISA and formulation (avx2)
    2.3x. The recompile to avx-512 then lands int8 4.5x ahead of ksw2
    and 3.3x ahead of parasail (int16: 2.9x ahead of parasail).
    Ablations at avx-512 (single-shot, verified per call): unfolded 2.1x
    (both formulations), rdom 3.8x (int8) / 4.4x (int16) of their
    inductive form.

    256x256, batch 1024, one thread (single-shot): int8 6.2, int16 8.0,
    ksw2 37.8, parasail 41.1 ms (5.3x / 5.7x with the 0.9 ms CIGAR
    encoding included); unfolded 1.1x / 1.5x, rdom 1.5x / 2.4x.

    1024x1024, batch 4096, all cores (PAR=true, baselines threaded
    across pairs), single-shot:
      int8 inductive     42.0 ms   (40.8 fill+trace at 105 Gcell/s + 1.2 cigar)
      int16 inductive    38.6 ms
      int8 / int16 unfolded    335 /  483 ms   (8.7x / 12.6x)
      int8 / int16 rdom        739 / 1076 ms   (18x / 26x)
      ksw2 sse           57.6 ms   (1.37x)
      parasail           46.1 ms   (1.10x)

Fairness, as audited. Vector width: ksw2 ships only 128-bit SSE
kernels and parasail's widest are AVX2 (no AVX-512 kernels exist; its
dispatcher picks avx2 here), while Halide compiles to AVX-512 - hence
the matched-ISA columns. Formulation: ksw2 uses int8 Suzuki-Kasahara
differences (our align8), parasail absolute int16 scores (our align16;
its int8 kernels would saturate at 1k lengths, exactly the overflow
the differences avoid). At each baseline's own ISA and formulation the
batch schedule is 2.1-2.3x ahead, from what intra-sequence iteration
pays and the batch does not (ksw2's per-diagonal profile fills,
byte-carry shifts and boundary bookkeeping; parasail's lazy-F pass and
per-call profile and result allocations), plus keeping four stripes of
pairs in flight - a single stripe is latency-bound and LOSES to ksw2.
Scope condition: inter-sequence SIMD needs a batch of comparable-length
pairs, the shape of an aligner's scoring stage; both baselines align
one pair per call. Verification: ksw2 is byte-exact (every CIGAR and
score, on every run); parasail is score-exact (its CIGARs can differ by
tie-breaking; all re-score optimal). parasail's trace table stores a
full 16-bit lane per cell - it does no direction packing.

In the parallel configuration the fill is bound by the memory
subsystem's write path: it scales linearly to 4 threads (8 Gcell/s per
thread), loses 30 percent per thread by 8, and caps at ~105 Gcell/s
from 32 threads on - one streaming-stored direction byte per cell. The
9970X is four CCDs with separate L3s and fabric links; spreading
threads across CCDs recovers 12-28 percent on the way up. There is no
cache-resident regime for a batch-width x N^2 block at 1k lengths (a
plane that fits total L3 is SLOWER with regular stores, 60 vs 91
Gcell/s, since it outruns any one CCD's 32 MB and pays
read-for-ownership). Parasail's per-call 2 MB trace is consumed and
freed - isolated, its resident set is 173 MB with no more DRAM fills
than ours - so it never pays that path; making our plane transient per
block and the walk vectorized is what moved the parallel row from 5
percent behind parasail to 10 percent ahead. The lever still untaken:
pack directions to four bits (parasail cannot; its trace lane is its
score width).

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
