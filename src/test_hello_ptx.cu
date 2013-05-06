/* Test: result = thread ID.
 *
 * CUDA equivalent of test_hello_ptx.ml kernel.
 */

__global__ void test(const float* input, float* result, int N)
{
    int i = blockDim.x * blockIdx.x + threadIdx.x;
    // if (i < N)
        result[i] = float(i);
}
