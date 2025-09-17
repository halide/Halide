import unittest

import onnx.backend.test

import halide_as_onnx_backend as halide_backend
import halide_as_onnx_backend_failures_table

backend_test = onnx.backend.test.BackendTest(halide_backend, __name__)

# These tests are simply too slow.
backend_test.exclude(r"test_densenet121_.*")
backend_test.exclude(r"test_inception_v1_.*")
backend_test.exclude(r"test_inception_v2_.*")
backend_test.exclude(r"test_resnet50_.*")
backend_test.exclude(r"test_squeezenet_.*")
backend_test.exclude(r"test_vgg19_.*")
backend_test.exclude(r"test_zfnet512_.*")

exclude_patterns = getattr(
    halide_as_onnx_backend_failures_table, "HALIDE_ONNX_KNOWN_TEST_FAILURES", []
)
for _, _, pattern in exclude_patterns:
    backend_test.exclude(pattern)

# import all test cases at global scope to make them visible to python.unittest
globals().update(backend_test.test_cases)

if __name__ == "__main__":
    unittest.main()
