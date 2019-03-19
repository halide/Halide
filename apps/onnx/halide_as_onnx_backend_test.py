from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import unittest
import onnx.backend.test
import halide_as_onnx_backend as halide_backend

# This is a pytest magic variable to load extra plugins
pytest_plugins = 'onnx.backend.test.report',

backend_test = onnx.backend.test.BackendTest(halide_backend, __name__)

backend_test.exclude(r'(test_hardsigmoid'  # Does not support Hardsigmoid.
                     '|test_hardmax'  # Does not support Hardmax.
                     '|test_depthtospace.*'  # Does not support DepthToSpace.
                     '|test_.*rnn.*'  # Does not support RNN
                     '|test_.*gru.*'  # Does not support GRU
                     '|test_.*PReLU.*'  # prelu activations not supported yet
                     '|test_.*prelu.*'  # prelu activations not supported yet
                     '|test_.*thresholdedrelu.*' # thresholdedrelu activiations not supported yet
                     '|test_.*repeat.*'  # Tiling not supported yet
                     '|test_.*pool_with_argmax.*'  # argmax not yet supported
                     '|test_.*pool.*same.*'  # same padding not yet supported
                     '|test_.*averagepool.*'  # AvgPool not fully supported
                     '|test_.*AvgPool.*'  # AvgPool not fully supported
                     '|test_.*unpool.*'  # unpool not yet supported
                     '|test_.*convtranspose.*'   # Not supported yet
                     '|test_.*ConvTranspose.*'   # Not supported yet
                     '|test_.*Conv.*_dilated.*'  # Not supported yet
                     '|test_.*mvn.*'  # MeanVarianceNormalization is not supported.
                     '|test_.*dynamic_slice.*'  # Slicing not supported yet
                     '|test_slice_start_out_of_bounds.*'
                     '|test_.*expand.*'  # Expand not supported yet
                     '|test_.*view.*'  # Expand not supported yet
		     '|test_reshape_*'
		     '|test_tile_*'
                     '|test_.*like.*'  # Needs implementation
                     '|test_.*ofshape.*'  # Needs implementation
                     '|test_.*Loss.*'  # Losses not supported yet
                     '|test_.*instancenorm.*'  # not supported yet
                     '|test_arg.*'  # not supported yet
                     '|test_top.*'  # not supported yet
                     '|test_compress_.*'  # not supported yet
                     '|test_where.*'  # not supported yet
                     '|test_shrink.*'  # not supported yet
                     '|test_nonzero.*'  # not supported yet
                     '|test_tfidfvectorizer.*'  # not supported yet
                     '|test_cast_FLOAT_to_STRING.*'  # not supported yet
                     '|test_cast_STRING_to_FLOAT.*'  # not supported yet
                     '|test_.*gather.*'  # not supported yet
                     '|test_.*scatter.*'  # not supported yet
                     '|test_.*matmul.*'  # not supported yet
                     '|test_.*upsample.*'  # not supported yet
                     '|test_.*lrn.*'  # not supported yet
                     '|test_scan.*'  # Scan not supported yet
                     '|test_strnorm.*'  # Scan not supported yet
                     '|test_operator_pow_cpu'  # Nan not handled properly
                     '|test_.*Linear.*'  # not supported yet
                     '|test_.*size.*'  # Size should return a scalar instead of a 1d tensor
                     '|test_.*index.*'  # Indexing not supported
                     '|test_.*GLU.*'  # GLU not supported
                     '|test_.*chunk.*'  # chunk not supported
                     '|test_.*symbolic.*'  # not supported yet
                     '|test_.*FLOAT16*'  # FLOAT16 not supported yet
                     '|test_.*cuda'  # Cuda not supported yet
                     '|test_.*Embedding.*'
                     '|test_.*alexnet.*'
                     '|test_.*vgg.*'
                     '|test_.*zfnet.*'
                     '|test_.*densenet.*'
                     '|test_.*resnet.*'
                     '|test_.*shufflenet.*'
                     '|test_.*squeezenet.*'
                     '|test_.*inception.*)'
                     )

# import all test cases at global scope to make them visible to python.unittest
globals().update(backend_test
                 .enable_report()
                 .test_cases)


if __name__ == '__main__':
    unittest.main()
