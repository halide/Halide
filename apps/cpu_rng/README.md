Fills a buffer with uniform random floats from xoshiro256++, the
generator behind Julia's default RNG: 256 bits of state per stream
advance every step, and each step yields two floats built from the
halves of its 64-bit result - the same use-all-the-bits bulk-fill
contract Julia's Random.XoshiroSimd runs. The state is large, evolving,
and never wanted by the consumer; only the projection is. Three forms
from one generator, bit-exact against a scalar reference:

- scan=inductive: the state walks in a two-deep folded window that
  never leaves registers; only the floats reach memory.
- scan=unfolded: the same fused walk with folding disabled, leaving the
  32-byte-per-step trajectory live. Isolates folding.
- scan=rdom: the walk as an update definition, which owns its axis, so
  the state trajectory must be materialized before the projection reads
  it.

Baselines, all the same generator: the canonical scalar loop, a
hand-written AVX-512 kernel (same interleaved-streams layout), and
Julia's rand! (which uses eight SIMD-interleaved substreams of the same
generator - the same technique; its exact output bits differ because
its substream seeding does). numpy's default fill is PCG64, a different
and smaller-state generator, reported for ecosystem context only.

Measured on a Threadripper 9970X (Zen 5), one thread, 1 GB of floats
(32 streams x 4M steps x 2):

    inductive       38.6 ms   (28 GB/s of output)
    hand AVX-512    36.0 ms   (0.93x)
    julia rand!     36.1 ms   (0.94x)
    unfolded       180 ms     (4.7x)
    rdom           370 ms     (9.6x)
    scalar C++     583 ms     (15.1x)
    numpy PCG64    500 ms     (13x, different generator)

Why the top three tie: they are the same algorithm at the same wall.
All keep the state in registers, advance eight 64-bit lanes per vector,
and project both halves with integer ops, so each is limited by a
single core's store bandwidth. The Halide version is thirty declarative
lines; the schedule is what recovers the other two implementations'
hand engineering.

What the schedule buys beyond parity: par=true spreads stream blocks
across cores and reaches the chip's write bandwidth - 23 ms at 46 GB/s
for the same gigabyte (1.6x past any single-thread implementation;
Julia's rand! is single-threaded, and going wider there means managing
per-task generators by hand. Use HL_NUM_THREADS near the physical core
count; oversubscription costs ~30 percent). And the ablations quantify
what the inductive form itself is worth: materializing the trajectory
an RDom walk requires costs 9.6x, and even the fused-but-unfolded form
costs 4.7x - at the store-bandwidth roofline, the state has no spare
bandwidth to hide in.
