from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import bin.model_cpp as model_cpp


class Model():
    def __init__(self):
        self.pipeline = None

    def BuildFromOnnxModel(self, onnx_model, device='unknown'):
        assert onnx_model

        if type(device) is str:
            device = device.encode()

        if type(onnx_model) is str:
            onnx_model = onnx_model.encode()
            self.pipeline = model_cpp.ConvertOnnxModel(onnx_model, device)
        elif type(onnx_model) is bytes:
            self.pipeline = model_cpp.ConvertOnnxModel(onnx_model, device)
        else:
            # protobuf don't support swig, so we have to convert them to string.
            model_str = onnx_model.SerializeToString()
            self.pipeline = model_cpp.ConvertOnnxModel(model_str, device)

    def OptimizeSchedule(self):
        if not self.pipeline:
            raise Exception("model not initialized, call BuildFromOnnxModel first")
        return model_cpp.AutoSchedule(self.pipeline)

    def run(self, inputs):
        if not self.pipeline:
            raise Exception("model not initialized, call BuildFromOnnxModel first")
        return model_cpp.Run(self.pipeline, inputs)

    def Benchmark(self, num_iters=5):
        if not self.pipeline:
            raise Exception("model not initialized, call BuildFromOnnxModel first")
        return model_cpp.Benchmark(self.pipeline, num_iters)
