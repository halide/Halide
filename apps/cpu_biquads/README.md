A cascade of N biquad filter sections over many audio channels - the
recurrence scipy.signal.sosfilt runs - on a signal chosen not to fit in
the last level cache. The same algorithm is built two ways from one
generator:

- scan=inductive: the cascade fuses into one streaming pass. Every
  section's two-sample window lives in a folded buffer that never leaves
  cache, and the channel vectors advance together through one serial walk
  so their independent recurrences hide the filter's latency chain.
- scan=rdom: each section is an update definition over an RDom. An update
  definition owns its walk, so each section must be computed whole before
  the next reads it: the signal crosses memory once per section. Tuned
  as far as the update form goes: the whole section is the update over
  an undefined pure definition (the walk writes every sample before it
  is read, so there is no fill sweep), one walk per section that reads
  the previous section and writes this one with the feedback taps still
  in cache - not a feed-forward sweep and a read-modify-write walk - and
  two channel vectors unrolled inside each walk so two recurrence chains
  are in flight. Leaving the pure definition undefined is the RDom form's
  only way to avoid the fill; the inductive form needs nothing of the
  kind.

Neither form streams its output: a filter's output is consumed as it is
produced, and the library baselines store theirs the ordinary way too.

Run `make test` to check both against a double precision reference and
benchmark them, and `python3 sosfilt_bench.py` for the scipy number.
Knobs: SECTIONS, CHANNELS, SAMPLES, PAR (spread channel blocks across
cores).

The numbers live in the table apps/inductive_benchmarks.py writes
(apps/inductive_benchmarks_results.md), measured on a Threadripper 9970X
(Zen 5, 128 MB L3) under one protocol for every form and baseline. At
eight sections over 32 channels x 8M samples (1 GB) on one core the
inductive form takes 95 ms and the RDom form 447 ms; over 1024 channels
x 256K samples on all 32 cores, 26 ms against 193 ms. The RDom form is
at its best here: every stage is data parallel over channels, so the
whole cascade nests inside the output's channel-block loop as one
parallel loop rather than a chain of root-level kernels, and its
remaining cost is exactly its traffic - 16 GB per run through memory,
and on one core that is the core's memory ceiling. Across cores it is
bound by each chiplet's write path to the memory controller: pinned to
one chiplet its eight tasks run no faster than one does, spread over
four they run twice as fast, which is why the driver places one thread
per physical core for every all-cores measurement. Both forms carry
their recurrence state through L1 with store-to-load forwarding (LLVM
does not promote loop-carried array elements to registers), so that is
not where the gap comes from.

Library baselines, same double reference: scipy.sosfilt (single thread,
scalar, float32) takes 2.0 s at 8 sections. Intel IPP has a
multi-channel IIR call, ippsIIR_32f_P, over per-channel biquad-cascade
states (the same sos taps; the runner builds it when IPP_DIR points at
a venv with ipp-devel installed): 1.6 s single-threaded at the same
shape - inside, it walks the channels one at a time, so it is not
vectorized across them either - and, dealt across the Halide thread
pool in the parallel configuration, 82 ms against the inductive form's
26. IPP
rounds a few ulps differently (its own single-precision filter
structure), so its check uses a looser tolerance. Inside, its AVX-512
dispatch variant uses xmm registers only: four-wide ops on one
channel's taps, one channel at a time, a latency-bound chain that the
parallel row hides with many threads (it needs every hardware thread
to reach 64 ms; the inductive form is at the memory wall from 8 cores
up). Julia's DSP.jl filt (1.95 s single
thread, 182 ms over 64 threads), torchaudio's lfilter (58 s) and
FFmpeg's biquad filter (2.0 s for 24 channels, its largest named
layout; handed more channels in an unnamed layout it silently passes
the signal through) are the same shape: every library walks channels
one at a time.

The one baseline vectorized across channels is built from the "Finding
Fast Filters" template library (Ma, Adams and Ragan-Kelley,
https://arxiv.org/abs/2607.20634), vendored under fff/; the app builds
with clang for its vector extensions (fff_biquads.cpp, compiled
-O3 -march=native without fast-math, which its authors measured to be
slower for it): a block of channels, channels innermost, is one 1-D
signal with that stride, each section a sparse FIR with taps at 0,
stride and 2*stride for the numerator cascaded with the library's
second-order IIR at that stride for the denominator. Its best shape
differs by configuration: serially, 32-channel blocks with the serial
IIR, two vector chains a step, 173 ms against the inductive form's 95;
across cores, 16-channel blocks with the pairwise IIR, whose shorter
dependency chain wins once every core has a single block, 25 ms against
26. On one core what separates it from the inductive form is the chain:
the library's IIR carries one recurrence per vector, so a block is
latency-bound, where the inductive schedule interleaves two channel
vectors through a window kept in L1. Across cores both sit at the
memory wall, and the table's fastest baseline is this one.

scan=unfolded isolates fusion from folding: the same fused pass with
every section's whole trajectory kept live (an explicit full-extent
fold, since Halide's automatic folding pass would otherwise fold it).
It takes 3.6x the folded form's time on one core and 6.4x across
cores, against the RDom form's 4.7x and 7.4x: the fused-but-unfolded
pass still writes every section's trajectory, so most of the RDom
form's loss is the traffic that folding removes, and the rest is the
walk it cannot interleave with anything.
