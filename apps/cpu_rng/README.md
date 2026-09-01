Fills a buffer with uniform random floats from xoshiro256++, the
generator family behind Julia's default RNG: 256 bits of state per
stream advance every step, and each step's output is a 24-bit projection
of that state. The state is large, evolving, and never wanted by the
consumer - only the projection is - which is exactly the shape where an
inductive Func earns its keep: the walk's state lives in a two-deep
folded window in registers, and only the floats reach memory.

Three forms from one generator, all bit-exact against a scalar
xoshiro256++ reference:

- scan=inductive: state in registers, output-only traffic.
- scan=unfolded: the same fused walk with folding disabled, so the full
  32-byte-per-step state trajectory is left live. Isolates folding.
- scan=rdom: the walk as an update definition, which owns its axis, so
  the state trajectory must be materialized before the projection reads
  it.

Measured on a Threadripper 9970X (Zen 5), 32 streams x 4M steps (537 MB
of floats, 4.3 GB of state per trajectory), single thread:

    inductive      23 ms   (23 GB/s of output)
    unfolded      162 ms   (7.0x)
    rdom          367 ms   (15.9x)
    scalar C++    502 ms   (21.8x)
    numpy PCG64   251 ms   (10.9x, rng.random(dtype=float32))

Parallel over 256 streams x 512K steps: inductive 12 ms at 45 GB/s of
output; unfolded 8.0x; rdom 15.0x; the scalar loop 49x.

Unlike the biquad cascade, where fusion is the whole win and folding
only bounds the footprint, here folding itself is worth 7x: the
inductive pass runs at the memory system's speed, so the unfolded
trajectory's extra gigabytes have no spare bandwidth to hide in. The
two apps bracket the design space between them.
