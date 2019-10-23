"""Verifies the Halide operator functions properly."""


import os
import unittest
import warnings

import torch as th
import modules


class TestAdd(unittest.TestCase):
    def setUp(self):
        self.a = th.ones(1, 2, 8, 8)
        self.b = th.ones(1, 2, 8, 8)*3
        self.gt = th.ones(1, 2, 8, 8)*4

    def test_cpu_single(self):
        self._test_add(is_double=False)

    def test_cpu_double(self):
        self._test_add(is_double=True)

    def test_gpu_single(self):
        if not th.cuda.is_available():
            return
        self._test_add(is_cuda=True, is_double=False)

    def test_gpu_double(self):
        if not th.cuda.is_available():
            return
        self._test_add(is_cuda=True, is_double=True)

    def _test_add(self, is_cuda=False, is_double=False):
        if is_double:
            self.a = self.a.double()
            self.b = self.b.double()
            self.gt = self.gt.double()
        if is_cuda:
            print("Testing Halide PyTorch CUDA operator...")
            self.a = self.a.cuda()
            self.b = self.b.cuda()
            self.gt = self.gt.cuda()
        else:
            print("Testing Halide PyTorch CPU operator...")

        output = modules.Add()(self.a, self.b)

        if is_double:
            print("  Double-precision mode")
        else:
            print("  Single-precision mode")

        diff = (output-self.gt).sum().item()
        assert diff == 0.0, "Test failed: sum should be 4, got %f" % diff

        # Test the gradient is correct
        self.a.requires_grad = True
        self.b.requires_grad = True

        with warnings.catch_warnings():
            # Inputs are float, the gradient checker wants double inputs and
            # will issue a warning.
            warnings.filterwarnings(
                "ignore", message="At least one of the inputs that requires "
                "gradient is not of double precision")
            res = th.autograd.gradcheck(modules.Add(), [self.a, self.b], eps=1e-2)

        print("  Test ran successfully: difference is", diff)


if __name__ == "__main__":
    unittest.main()
