# Times Julia's stdlib bulk fill of the same-shaped Float32 array. Julia's
# default RNG is xoshiro256++, and Random.XoshiroSimd fills arrays with
# eight SIMD-interleaved substreams - the same generator and the same
# multi-stream technique this benchmark uses. Run with julia -t N to fill
# blocks of streams in parallel.
using Random
# L counts float rows: two per stream, so 2x the generator's lanes param.
L = length(ARGS) >= 1 ? parse(Int, ARGS[1]) : 64
T = length(ARGS) >= 2 ? parse(Int, ARGS[2]) : 4 << 20
nt = Threads.nthreads()
if nt == 1
    rng = Xoshiro(1234)
    a = Array{Float32}(undef, L, T)
    fill!() = rand!(rng, a)
else
    # One generator per block of eight substreams (16 float rows), the
    # blocks spread over the threads. rand!'s SIMD path needs a contiguous
    # destination, so each block fills its own 16 x T slab: the same bytes
    # as the single array, blocked by stream group rather than by step.
    NB = L ÷ 16
    L % 16 == 0 || error("threaded fill needs L to be a multiple of 16")
    rngs = [Xoshiro(1234 + k) for k in 1:NB]
    a = Array{Float32}(undef, 16, T, NB)
    slabs = [unsafe_wrap(Array, pointer(a, (k - 1) * 16 * T + 1), (16, T)) for k in 1:NB]
    fill!() = Threads.@threads for k in 1:NB
        rand!(rngs[k], slabs[k])
    end
end
# The shared protocol (apps/support/bench_harness.h): HB_WARMUP untimed
# runs, HB_TRIALS timed ones, the best reported.
for _ in 1:parse(Int, get(ENV, "HB_WARMUP", "3"))
    fill!()
end
best = Inf
for _ in 1:parse(Int, get(ENV, "HB_TRIALS", "30"))
    t0 = time_ns()
    fill!()
    global best = min(best, (time_ns() - t0) / 1e3)
end
using Printf
@printf("  julia xoshiro %10.1f us  (Float32 rand!, %s)\n", best,
        nt == 1 ? "single thread" : "$nt threads, one generator per 8-stream block")
