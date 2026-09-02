Times are milliseconds per run: inductive = the folded inductive-Func form, RDom = the same algorithm as update definitions (materializing), baseline = the fastest non-Halide implementation available on this machine. Measured on a Threadripper 9970X (Zen 5, 32 cores) and an RTX 5060 Ti.

Every Halide form is verified against its app's reference before timing (the alignment rows byte-exact against ksw2, the rng rows bit-exact against Julia's rand! seeding, the GPU rows against serial references). Baselines are threaded across independent problems in the all-cores rows where the library allows it (ksw2, parasail, oneTBB); the rng hand kernel and Julia's rand! are single-threaded, so the rng all-cores baseline is the single-thread hand kernel. The mamba2 rows compare each side at its own best chunk (Triton prefers 256; the Halide backward is best at 128), with the tensor-core schedules (WMMA=true). Flash attention's RDom form is the same online softmax with the running maximum and row sum carried at the accumulator's shape, broadcast across its columns, so that one Tuple update over the key chunks advances all three (Halide fuses no dependent stages, and a per-row Func may not read the state its update feeds); the rescalings are then paid per element rather than per row, the same tile verbs and staging otherwise. For scale, cuBLAS + softmax + cuBLAS is about 13x slower than the flash filter. Chebyshev is the intended in-cache control, where folding buys nothing. Outputs that are written once and never read back are streamed in every form (rng, biquads, the alignment direction plane); the JIT apps (kalman, viterbi, ode) reuse their scratch buffers across timed runs so page faults on fresh mappings are not charged to any form; the Python baselines report the best sample, as Halide's harness does.

| App | Config | Inductive (ms) | RDom (ms) | Fastest baseline (ms) | Baseline | RDom / ind | Baseline / ind |
|---|---|---:|---:|---:|---|---:|---:|
| cpu_biquads | 8 sections, 32 ch x 8388608 samples, 1 thread | 116.7 | 533.0 | 1636.7 | Intel IPP ippsIIR_32f_P | 4.57x | 14.03x |
| cpu_biquads | 8 sections, 256 ch x 1048576 samples, all cores | 26.4 | 465.0 | 37.0 | Intel IPP ippsIIR_32f_P (threaded) | 17.62x | 1.40x |
| cpu_rng | 32 streams x 4194304 steps, 1 thread | 23.8 | 363.9 | 24.9 | hand AVX-512 kernel | 15.27x | 1.04x |
| cpu_rng | 1024 streams x 131072 steps, all cores | 17.7 | 166.5 | 32.5 | hand AVX-512 kernel | 9.43x | 1.84x |
| cpu_alignment | 1024x1024 x 128 pairs, 1 thread, fill+traceback+cigar | 10.3 | 45.7 | 43.1 | parasail | 4.42x | 4.17x |
| cpu_alignment | 1024x1024 x 4096 pairs, all cores, fill+traceback+cigar | 37.8 | 711.7 | 46.2 | parasail | 18.81x | 1.22x |
| kalman_ll | 256 series x 16384 steps, 1 thread | 1.3 | 3.7 | - | - | 2.75x | - |
| kalman_ll | 256 series x 16384 steps, 32 threads | 0.190 | 1.9 | - | - | 9.91x | - |
| viterbi | 16 states, 4 symbols, T=320000, 1 thread | 5.7 | 7.7 | - | - | 1.35x | - |
| viterbi | 64 states, 8 symbols, T=50000, 1 thread | 8.4 | 10.2 | - | - | 1.22x | - |
| ode | Allen-Cahn D=1024, batch 1, T=32768, 1 thread | 6.0 | 8.4 | 34.4 | Boost.odeint | 1.39x | 5.71x |
| prefixsum | 1048576 x 32 rows, running-mean consumer, 1 thread | 37.3 | 45.6 | 44.4 | oneTBB parallel_scan | 1.22x | 1.19x |
| prefixsum | 1048576 x 32 rows, running-mean consumer, 32 threads | 2.8 | 3.8 | 3.3 | oneTBB parallel_scan | 1.34x | 1.16x |
| chebyshev | n=2048 dense SPD, 100 iterations, 1 thread (in-cache control) | 144.2 | 138.5 | 138.4 | hand-written mod-3 ring | 0.96x | 0.96x |
| cuda_mamba2 | fwd, seq 4096, state 128, 128 heads x 64, chunk 256 (Triton 256), RTX 5060 Ti | 1.6 | 1.7 | 1.6 | Triton (mamba_ssm) | 1.08x | 1.00x |
| cuda_mamba2 | bwd, seq 4096, state 128, 128 heads x 64, chunk 128 (Triton 256), RTX 5060 Ti | 4.4 | 5.6 | 4.4 | Triton (mamba_ssm) | 1.27x | 0.99x |
| flash_attention | 65536 queries x 1024 keys, depth 64, fp16, chunk 64, RTX 5060 Ti | 0.395 | 0.435 | 0.381 | FlashAttention-2 (torch SDPA) | 1.10x | 0.96x |
