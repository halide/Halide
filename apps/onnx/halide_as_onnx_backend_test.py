
import unittest
import onnx.backend.test
import halide_as_onnx_backend as halide_backend

# This is a pytest magic variable to load extra plugins
#pytest_plugins = 'onnx.backend.test.report',

backend_test = onnx.backend.test.BackendTest(halide_backend, __name__)

exclude_test_patterns = (
    r'(test_hardsigmoid'  # Does not support Hardsigmoid.
     '|test_hardmax'  # Does not support Hardmax.
     '|test_depthtospace.*'  # Does not support DepthToSpace.
     '|test_.*pool_with_argmax.*'  # argmax not yet supported
     '|test_.*pool.*same.*'  # same padding not yet supported
     '|test_.*unpool.*'  # unpool not yet supported
     '|test_.*convtranspose.*'   # Not supported yet
     '|test_.*ConvTranspose.*'   # Not supported yet
     '|test_.*Conv.*_dilated.*'  # Not supported yet
     '|test_maxpool.*dilation.*'  # MaxPool doesn't support dilation yet
     '|test_.*mvn.*'  # MeanVarianceNormalization is not supported.
     '|test_.*pool.*ceil.*'  # ceil mode is not supported yet
     '|test_.*like.*'  # Needs implementation
     '|test_.*instancenorm.*'  # not supported yet
     '|test_reshape.*'
     '|test_shrink.*'
     '|test_tile.*'
     '|test_dynamic_slice.*'  # not supported yet
     '|test_arg.*'  # not supported yet
     '|test_top.*'  # not supported yet
     '|test_resize.*'  # opset 10 is not supported yet
     '|test_compress_.*'  # not supported yet
     '|test_nonzero.*'  # not supported yet
     '|test_tfidfvectorizer.*'  # not supported yet
     '|test_cast_FLOAT_to_STRING.*'  # not supported yet
     '|test_cast_STRING_to_FLOAT.*'  # not supported yet
     '|test_.*scatter.*'  # not supported yet
     '|test_.*upsample.*'  # not supported yet
     '|test_.*qlinear.*'  # skip quantized op test
     '|test_.*quantize.*'  # skip quantized op test
     '|test_.*matmulinteger.*'  # skip quantized op test
     '|test_.*convinteger.*'  # skip quantized op test
     '|test_mod_.*'  # not supported yet
     '|test_cumsum_.*'  # not supported yet
     '|test_bitshift_.*'  # not supported yet
     '|test_nonmaxsuppression.*'  # not supported yet
     '|test_reversesequence.*'  # not supported yet
     '|test_roialign.*'  # not supported yet
     '|test_round.*'  # not supported yet
     '|test_scan.*'  # Scan not supported yet
     '|test_strnorm.*'  # not supported yet
     '|test_.*index.*'  # Indexing not supported
     '|test_.*chunk.*'  # chunk not supported
     '|test_operator_symbolic_override.*'  # InstanceNormalization not supported yet
     '|test_.*FLOAT16*'  # FLOAT16 not supported yet
     '|test_densenet.*'   # Times out
     '|test_inception_v2.*'  # Padding type not supported for pooling
     ')'
)


backend_test.exclude(exclude_test_patterns)

# import all test cases at global scope to make them visible to python.unittest
globals().update(backend_test
                 .enable_report()
                 .test_cases)


if __name__ == '__main__':
    unittest.main()
