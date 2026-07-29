# Tensor core resize

Resampling an image is a linear operator, so it can be written as a matrix
multiply. The matrix is enormous and almost entirely zero, so you never want to
materialize it, but each of its rows has a small number of contiguous
non-zeros. If neighbouring rows are made to share a starting column (which just
means storing a few more zeros), then a block of 16 rows of it is a small dense
matrix, and the inner loop becomes a matrix multiply. That's a large speed-up
even without tensor cores, and it lets us use tensor cores when we have them.

This is the algorithmic difference between this generator and the one in
`apps/resize`, which starts each row of the matrix at its own column.

## Status

Both schedules work. On an RTX 5060 Ti, downsampling a 3840x2160 image by 4x
with a Lanczos kernel takes 0.359 ms with the `cudaonly` schedule and 0.246 ms
with the `tensorcore` one, a 1.46x speed-up.

Both schedules round the output size up to a multiple of 16, so the runner
gives them an output buffer of that size.
