from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import model_cpp


class Model():
    def __init__(self):
        self.pipeline = None

    def BuildFromOnnxModel(self, onnx_model, expected_dim_sizes=None,
                           layout=model_cpp.Layout.NumPy):
        assert onnx_model
        if not expected_dim_sizes:
            expected_dim_sizes = {}

        if type(onnx_model) is str:
            onnx_model = onnx_model.encode()
            self.pipeline = model_cpp.ConvertOnnxModel(onnx_model,
                expected_dim_sizes, layout)
        elif type(onnx_model) is bytes:
            self.pipeline = model_cpp.ConvertOnnxModel(onnx_model,
                expected_dim_sizes, layout)
        else:
            # protobuf don't support swig, so we have to convert them to string.
            model_str = onnx_model.SerializeToString()
            self.pipeline = model_cpp.ConvertOnnxModel(model_str,
                expected_dim_sizes, layout)

    def OptimizeSchedule(self):
        if not self.pipeline:
            raise Exception("model not initialized, call BuildFromOnnxModel first")
        return model_cpp.AutoSchedule(self.pipeline)

    def run(self, inputs, device=''):
        if not self.pipeline:
            raise Exception("model not initialized, call BuildFromOnnxModel first")
        return model_cpp.Run(self.pipeline, inputs, device)

    def Benchmark(self, num_iters=5, device=''):
        if not self.pipeline:
            raise Exception("model not initialized, call BuildFromOnnxModel first")
        return model_cpp.Benchmark(self.pipeline, num_iters, device)

    def Compile(self, func_name, lib_name):
        if not self.pipeline:
            raise Exception("model not initialized, call BuildFromOnnxModel first")
        return model_cpp.Compile(self.pipeline, func_name, lib_name)

    def PrintLoopNest(self):
        if not self.pipeline:
            raise Exception("model not initialized, call BuildFromOnnxModel first")
        model_cpp.PrintLoopNest(self.pipeline)

    def PrintLoweredStatement(self):
        if not self.pipeline:
            raise Exception("model not initialized, call BuildFromOnnxModel first")
        model_cpp.PrintLoweredStatement(self.pipeline)
