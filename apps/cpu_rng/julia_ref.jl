# Dumps what Random.rand! actually computes so the Halide output can be
# checked against it byte for byte. rand! on a Xoshiro forks eight SIMD
# substream states from the master RNG (XoshiroSimd.forkRand): eight
# draws per state word, each multiplied by a fixed odd constant. The
# same draws replayed here give the seeds; a second fresh master run
# through rand! gives the reference bytes.
using Random
T = parse(Int, ARGS[1])
seedfile = ARGS[2]
reffile = ARGS[3]
K = (0x02011ce34bce797f, 0x5a94851fb48a6e05, 0x3688cf5d48899fa7, 0x867b4bb4c42e5661)
rng = Xoshiro(1234)
draws = [rand(rng, UInt64) for _ in 1:32]
open(seedfile, "w") do io
    for l in 1:8, i in 1:4
        write(io, htol(K[i] * draws[(i - 1) * 8 + l]))
    end
end
rng = Xoshiro(1234)
a = Array{Float32}(undef, 16, T)
rand!(rng, a)
write(reffile, a)
