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
- unfolded: the same fused walk with folding disabled.
- rdom: the walk as update definitions, which own their axes, so the
  whole score state materializes at full extent per pair before the
  directions can be derived. Nothing smaller is expressible: an update
  definition writes only its own buffer, so the direction byte must be
  a separate Func that reads the materialized scores back - and the
  Halide profiler shows that read-back, not the table fill, is where
  the time goes (at 1kx1k the consumer reading the table is ~17x the
  cost of writing it). That read-back is the materialization the fused
  forms avoid by deriving directions inline as the window rolls.

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

Measured on a Threadripper 9970X (Zen 5). Every number below is
FILL+CIGAR: the baselines' timed calls include their O(N) traceback,
so ours includes the runner's CIGAR walk over the batch-major
direction plane (about 14 percent of the fill single-threaded). The
baseline comparisons are interleaved single-shot medians (INTERLEAVE=9
rounds cycling int8, int16, ksw2, parasail) so all sides share thermal
and clock state - single-shot numbers taken cold flatter whichever
side runs first by up to 25 percent.

    1024x1024, batch 128, one thread, fill+cigar, interleaved medians:

      ISA the generators were compiled for:   sse4.1    avx2    avx-512
      int8  inductive (align8)                 35.4    24.1     19.8 ms
      int16 inductive (align16)                  -     32.5     22.9 ms
      ksw2 gg2_sse    (128-bit, int8 diffs)    67.6    67.5     67.8 ms
      parasail striped_16 (256-bit avx2, int16)  -     47.5     48.0 ms

    The like-for-like cells: ksw2 vs int8 at sse4.1 (its ISA and its
    formulation) 1.91x; parasail vs int16 at avx2 (its ISA and its
    formulation) 1.46x. The recompile to avx-512 then adds 1.4-1.8x:
    3.4x ahead of ksw2, 2.4x ahead of parasail with int8, 2.1x with
    int16. Ablations at avx-512 (single-shot, same run): unfolded 2.2x
    and rdom 3.7x (int8) / 2.5x and 5.4x (int16) of their inductive.

    256x256, batch 1024, one thread (single-shot): int8 8.3, int16 9.5,
    ksw2 38, parasail 42 ms; rdom 1.5x/2.0x, unfolded 1.2x/1.5x.

    1024x1024, batch 4096, all cores (PAR=true, baselines threaded
    across pairs), fill+cigar, single-shot:
      int8 inductive     49.5 ms   (43.7 fill at 98 Gcell/s + 5.8 cigar)
      int16 inductive    49       ms
      ksw2 sse           57.7 ms   (1.17x)
      parasail           47.0 ms   (0.95x - parasail is AHEAD here)
      unfolded / rdom    ~480 / ~1070 ms (11x / 24x)

Fairness, as audited. Vector width: ksw2 ships only 128-bit SSE
kernels and parasail's widest are AVX2 (no AVX-512 kernels exist;
its dispatcher picks avx2 here), while Halide compiles to AVX-512 -
hence the matched-ISA columns. Formulation: ksw2 uses int8
Suzuki-Kasahara differences (our align8), parasail uses absolute int16
scores (our align16; its int8 kernels would saturate at 1k lengths,
which is precisely the overflow the differences avoid). The advantage
this benchmark claims is not the inner loop: at each baseline's own
ISA and formulation the batch schedule is 1.5-1.9x ahead, from what
the intra-sequence iteration pays and the batch does not (ksw2's
per-diagonal profile fills, byte-carry shifts and boundary
bookkeeping; parasail's lazy-F correction pass and per-call profile
and result allocations), plus the ILP of keeping four stripes of pairs
in flight (a single stripe is latency-bound and LOSES to ksw2). The
recompile is the rest. Scope condition: inter-sequence SIMD needs a
batch of comparable-length pairs, the shape of an aligner's scoring
stage; both baselines align one pair per call. Verification strength:
ksw2 is byte-exact (every CIGAR and score), parasail is score-exact
(its CIGARs can differ by tie-breaking; all re-score optimal).

In the parallel configuration parasail comes out 5 percent ahead, and
the investigation of why corrects an earlier guess in this file. Our
parallel fill is bound by the memory subsystem's write path: it scales
linearly to 4 threads (8 Gcell/s per thread), loses 30 percent per
thread by 8, and caps at ~105 Gcell/s from 32 threads on - one
direction byte per cell as streaming stores, ~105 GB/s. The 9970X is
four CCDs with separate L3s and fabric links, and spreading threads
across CCDs recovers 12-28 percent on the way up, so the approach to
the ceiling is topology, not code quality. There is no cache-resident
regime to retreat to: a 128 MB plane written with regular stores is
SLOWER (60 vs 91 Gcell/s at 32 threads), because a batch-width x N^2
block outruns any one CCD's 32 MB L3 and pays read-for-ownership on
top. Parasail escapes the wall by contract rather than by bandwidth:
each call's 2 MB trace table (it stores a full 16-bit lane per cell,
no packing) is consumed by the CIGAR walk and freed, and measured in
isolation its resident set is 173 MB with no more DRAM fills than
ours - the traces never leave cache. Our contract materializes the
whole batch's plane (4.3 GB) and pays DRAM for it once. Parasail's
per-pair contiguous layout also makes its CIGAR walk nearly free where
our batch-major plane costs 12 percent. The levers this leaves, both
untaken: pack directions to four bits (halves the bytes at the wall;
parasail cannot, its trace lane is its score width), and trace each
block while its plane is hot instead of retaining the batch's.

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
