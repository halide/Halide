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
memory system's speed. scipy.sosfilt (single thread, scalar, float32)
takes 2.0 s at 8 sections. Parallel over 256 channels x 1M samples the
inductive form runs 26 ms at ~80 GB/s of signal against 450 ms for the
N-pass form, whose fills and walks all queue on the same DRAM.

scan=unfolded isolates fusion from folding: the same fused pass with
every section's whole trajectory kept live times the same as the folded
form (1.00x). The RDom form's loss is therefore all fusion: an update
definition owns its walk, so nothing can interleave with it, and the
signal must cross memory once per section. Folding is what keeps the
fused form's footprint at kilobytes instead of gigabytes.
