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

    inductive       35.2 ms   (31 GB/s of output)
    hand AVX-512    36.4 ms   (1.03x)
    julia rand!     36.1 ms   (1.03x)
    unfolded       180 ms     (5.1x)
    rdom           377 ms     (10.7x)
    scalar C++     591 ms     (16.8x)
    numpy PCG64    500 ms     (14x, different generator)

Why the top three tie: they are the same algorithm at the same wall.
All keep the state near registers, advance eight 64-bit lanes per
vector, and project both halves with integer ops, so each is limited
by a single core's store bandwidth. The interleaved half-extraction
must be written with extract_bits, which compiles to a free vector
reinterpret; a select on the lane parity costs three shuffles per
store. And unrolling the walk by the fold factor, which turns the
rolling window's modulo indices into constants the compiler keeps in
registers, requires the EXPLICIT slide directive - slide(y, t) - not
the implicit sliding-window pass: once the walk is split you are
sliding over a split dimension, which only the explicit machinery
handles (the implicit pass lowers the warm-up into per-iteration
dynamic catch-up loops instead, 40-60 percent slower). With slide, a
RoundUp split by the fold factor, and the pair unrolled, the steady
state is straight-line with no modulos and the window promotes: the
Halide version edges out the hand-written kernel.
To see it: make bin/host/rng.generator && ./bin/host/rng.generator \
-g rng -f rng_ind -e stmt -o /tmp target=host lanes=32 \
scan=inductive par=false

What the schedule buys beyond parity: par=true spreads stream blocks
across cores and reaches the chip's write bandwidth - 21 ms at 51 GB/s
for the same gigabyte with 1024 streams (1.7x past any single-thread
implementation; Julia's rand! is single-threaded, and going wider
there means managing per-task generators by hand). Thread-count
defaults are safe: the runtime parks excess workers, and adding
threads past the useful count does not regress. Provide enough stream
blocks - one task per sixteen output rows. And the ablations quantify
what the inductive form itself is worth: materializing the trajectory
an RDom walk requires costs 9.9x, and even the fused-but-unfolded form
costs 4.8x - at the store-bandwidth roofline, the state has no spare
bandwidth to hide in.
