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

The `cudaonly` schedule works. The `tensorcore` schedule currently only works
for the first of the two stages (the resample in y). The resample in x fails
with:

```
Matrix multiply not recognized. [...] the matrix multiply operands are not
loads with affine indices.
```

The load index for that stage contains `begin_of((x / 16) * 16)`, i.e. the
starting column of this block of 16 rows of the matrix. That subexpression is
uniform across the 16 lanes of a tile, but the simplifier leaves it as
`ceil_f32` applied to `(ramp(block * 16, 1, 16) / 16) * 16` rather than folding
the divide and multiply away into a broadcast, so `is_multiramp` can't see that
it is uniform and the index isn't recognized as affine.

Fixing this needs the simplifier to fold `ramp(a * k, 1, k) / k` down to
`broadcast(a, k)` when the ramp is nested inside another vector, after which the
lane-uniform recognition in `is_multiramp` handles the rest.
