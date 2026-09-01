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
  the next reads it: the signal crosses memory once per section.

Run `make test` to check both against a double precision reference and
benchmark them, and `python3 sosfilt_bench.py` for the scipy number.
Knobs: SECTIONS, CHANNELS, SAMPLES, PAR (spread channel blocks across
cores).

Measured on a Threadripper 9970X (Zen 5, 128MB L3), 32 channels x 8M
samples (1GB), single thread, via the runner's reusing allocator so the
N-pass form's per-run intermediate allocations do not add page-fault
time:

    sections          2        4        8       16
    inductive      59 ms   107 ms   151 ms   272 ms
    rdom          379 ms   772 ms  1501 ms  3087 ms
    ratio           6.4x     7.2x    10.0x    11.4x

At two sections the inductive pass streams at 36 GB/s - the memory
system's speed. scipy.sosfilt (single thread, scalar, float32) takes
2.1 s at 8 sections. Parallel over 256 channels x 1M samples the
inductive form runs 46 ms at 46 GB/s against 461 ms for the N-pass form.
