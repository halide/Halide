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
  as far as the update form goes: the whole section is the update (a
  zero-fill pure definition, streamed, then one walk that reads the
  previous section and writes this one with the feedback taps still in
  cache - not a feed-forward sweep and a read-modify-write walk), two
  channel vectors unrolled inside each walk so two recurrence chains are
  in flight, and the output streamed. An undefined pure definition would
  drop the fill sweep too (1.9x / 2.8x at 2 / 8 sections) but is unsafe
  Halide and not used. The walk's own stores cannot stream: it reloads
  what it just wrote.

Run `make test` to check both against a double precision reference and
benchmark them, and `python3 sosfilt_bench.py` for the scipy number.
Knobs: SECTIONS, CHANNELS, SAMPLES, PAR (spread channel blocks across
cores).

Measured on a Threadripper 9970X (Zen 5, 128MB L3), 32 channels x 8M
samples (1GB), single thread, via the runner's reusing allocator so the
N-pass form's per-run intermediate allocations do not add page-fault
time:

    sections          2        4        8       16
    inductive      51 ms    75 ms   135 ms   358 ms
    rdom          141 ms   278 ms   545 ms  1081 ms
    ratio           2.8x     3.7x     4.0x     3.0x

(The 8-section inductive time wanders 129-147 ms run to run; the RDom
time holds at 540-555.) The RDom form is at its best here: every stage
is data parallel over channels, so the whole cascade nests inside the
output's channel-block loop as one parallel loop rather than a chain of
root-level kernels, and its remaining cost is exactly its traffic. At
two sections it moves twice the bytes plus a fill sweep and lands at
2.8x; at eight it moves 24 GB at ~35 GB/s, a single core's memory
ceiling, while the inductive form is compute-bound on its eight-deep
chain per sample. Both forms carry their recurrence state through L1
with store-to-load forwarding (LLVM does not promote loop-carried array
elements to registers), so that is not where the gap comes from.

At two sections the inductive pass streams at 42 GB/s each way - the
memory system's speed. Parallel over 256 channels x 1M samples the
inductive form runs 26 ms at ~80 GB/s of signal against 450 ms for the
N-pass form, whose fills and walks all queue on the same DRAM.

Library baselines, same double reference: scipy.sosfilt (single thread,
scalar, float32) takes 2.0 s at 8 sections. Intel IPP has a
multi-channel IIR call, ippsIIR_32f_P, over per-channel biquad-cascade
states (the same sos taps; the runner builds it when IPP_DIR points at
a venv with ipp-devel installed): 1.64 s single-threaded at the same
shape - inside, it walks the channels one at a time, so it is not
vectorized across them either - and, dealt across threads in the
parallel configuration, 36 ms against the inductive form's 27. IPP
rounds a few ulps differently (its own single-precision filter
structure), so its check uses a looser tolerance. Inside, its AVX-512
dispatch variant uses xmm registers only: four-wide ops on one
channel's taps, one channel at a time, a latency-bound chain that the
parallel row hides with 64 hardware threads (pinned to 32 cores it
takes 88 ms, to 8 cores 209 ms, while the inductive form is flat from 8
cores up, at the memory wall). Julia's DSP.jl filt (1.95 s single
thread, 182 ms over 64 threads), torchaudio's lfilter (58 s) and
FFmpeg's biquad filter (2.0 s for 24 channels, its largest named
layout; handed more channels in an unnamed layout it silently passes
the signal through) are the same shape: every library walks channels
one at a time.

The one baseline vectorized across channels is built from the "Finding
Fast Filters" template library (Ma et al.), vendored under fff/ and
compiled with clang (fff_biquads.cpp): a block of channels, channels
innermost, is one 1-D signal with that stride, each section a sparse
FIR with taps at 0, stride and 2*stride for the numerator cascaded with
the library's second-order IIR at that stride for the denominator. Its
best shape differs by configuration: serially, 32-channel blocks with
the serial IIR, two vector chains a step, 204 ms; across cores,
16-channel blocks with the pairwise IIR, whose shorter dependency chain
wins once every core has a single block, 30 to 35 ms. What separates it
from the inductive form is the chain: the library's IIR carries one
recurrence per vector, so a block is latency-bound, where the inductive
schedule interleaves two channel vectors through a window kept in L1
and is bound by the memory system instead.

scan=unfolded isolates fusion from folding: the same fused pass with
every section's whole trajectory kept live times the same as the folded
form (1.00x). The RDom form's loss is therefore all fusion: an update
definition owns its walk, so nothing can interleave with it, and the
signal must cross memory once per section. Folding is what keeps the
fused form's footprint at kilobytes instead of gigabytes.
