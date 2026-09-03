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

Baselines, all the same generator: the canonical scalar loop, Julia's
rand! (Random.XoshiroSimd: eight SIMD-interleaved substreams of the same
generator, the same technique), and that kernel ported to AVX-512
intrinsics in runner.cpp (from stdlib/Random/src/XoshiroSimd.jl, Julia
1.12, whose 8 x UInt64 vectors LLVM already lowers to AVX-512 on this
CPU; the port runs threaded over blocks of streams under PAR, and the
all-cores Julia number fills blocks in parallel the same way, one
generator each). The Halide pipeline is bit-exact with Julia:
`make clean && make LANES=8 test_julia` seeds our eight streams exactly
as rand! forks its substreams from Xoshiro(1234) and checks the output
byte for byte (julia_ref.jl replays the fork; the projection is Julia's:
top 24 bits of each half, converted and scaled by 2^-24). numpy's
default fill is PCG64, a different and smaller-state generator, reported
for ecosystem context only.

Measured on a Threadripper 9970X (Zen 5), one thread, 1 GB of floats
(32 streams x 4M steps x 2):

    inductive       34.8 ms   (31 GB/s of output)
    Julia port      35.5 ms   (1.02x, AVX-512 intrinsics)
    julia rand!     35.4 ms   (1.02x)
    unfolded       180 ms     (5.2x)
    rdom           476 ms     (13.7x)
    scalar C++     591 ms     (17x)
    numpy PCG64    500 ms     (14x, different generator)

Why the top three tie: they are the same algorithm at the same wall.
All keep the state near registers, advance eight 64-bit lanes per
vector, and project both halves of each word with a shift, a convert,
and a scale, so each is limited by a single core's store bandwidth.
Two things keep the inner loop clean. The step's arithmetic must
happen at the state's own lane width, before each 64-bit result fans
out to a pair of output lanes - hence r64 and r32 are Funcs of their
own rather than expressions inside y, whose 16-wide vectors would
duplicate every add and rotate. And the interleaved half-extraction
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
Halide version edges out the ported kernel.
To see it: make bin/host/rng.generator && ./bin/host/rng.generator \
-g rng -f rng_ind -e stmt -o /tmp target=host lanes=32 \
scan=inductive par=false

What the schedule buys beyond parity: par=true spreads stream blocks
across cores and reaches the chip's write bandwidth - 21 ms at 51 GB/s
for the same gigabyte with 1024 streams (1.7x past any single-thread
implementation; rand! fills one array from one generator, so going
wider there means one generator per block of streams, which is what
julia_bench.jl does under julia -t). Thread-count
defaults are safe: the runtime parks excess workers, and adding
threads past the useful count does not regress. Provide enough stream
blocks - one task per sixteen output rows. And the ablations quantify
what the inductive form itself is worth: materializing the trajectory
an RDom walk requires costs 9.9x, and even the fused-but-unfolded form
costs 4.8x - at the store-bandwidth roofline, the state has no spare
bandwidth to hide in.
