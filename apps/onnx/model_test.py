from __future__ import absolute_import, division, print_function, unicode_literals
import unittest
from model import Model
from onnx import helper
from onnx import TensorProto
import numpy as np


class ModelTest(unittest.TestCase):
    def setUp(self):
        pass 
    def test_empty_model(self):
        model = Model()
        with self.assertRaises(Exception):
            model.GenerateSchedule() 
    def test_small_model(self):
        # Create one input
        X = helper.make_tensor_value_info('X', TensorProto.FLOAT, [2, 3])
        # Create one output
        Y = helper.make_tensor_value_info('Y', TensorProto.FLOAT, [2, 3])
        # Create a node
        node_def = helper.make_node('Abs', ['X'], ['Y'])

        # Create the model
        graph_def = helper.make_graph([node_def], "test-model", [X], [Y])
        onnx_model = helper.make_model(graph_def,
                                       producer_name='onnx-example')

        model = Model()
        model.BuildFromOnnxModel(onnx_model)
        schedule = model.OptimizeSchedule()
        schedule = schedule.replace('\n', ' ')
        print("DEBUG:", schedule)
        expected_schedule = r'// Target: .+// MachineParams: .+// Delete this line if not using Generator Pipeline pipeline = get_pipeline\(\);.+Func Y_1 = pipeline.get_func\(1\);.+{.+}.+'
        self.assertRegex(schedule, expected_schedule)

        input = np.random.rand(2, 3) - 0.5
        outputs = model.run([input])
        self.assertEqual(1, len(outputs))
        output = outputs[0]
        expected = np.abs(input)
        np.testing.assert_allclose(expected, output) 
    def test_scalars(self):
        # Create 2 inputs
        X = helper.make_tensor_value_info('X', TensorProto.INT32, [])
        Y = helper.make_tensor_value_info('Y', TensorProto.INT32, [])
        # Create one output
        Z = helper.make_tensor_value_info('Z', TensorProto.INT32, [])
        # Create a node
        node_def = helper.make_node('Add', ['X', 'Y'], ['Z'])

        # Create the model
        graph_def = helper.make_graph([node_def], "scalar-model", [X, Y], [Z])
        onnx_model = helper.make_model(graph_def,
                                       producer_name='onnx-example')

        model = Model()
        model.BuildFromOnnxModel(onnx_model)
        schedule = model.OptimizeSchedule()
        schedule = schedule.replace('\n', ' ')
        print("DEBUG:", schedule)
        expected_schedule = r'// Target: .+// MachineParams: .+// Delete this line if not using Generator Pipeline pipeline = get_pipeline\(\);.+Func Z = pipeline.get_func\(2\);.+{.+}.+'
        self.assertRegex(schedule, expected_schedule)

        input1 = np.random.randint(-10, 10, size=())
        input2 = np.random.randint(-10, 10, size=())
        outputs = model.run([input1, input2])
        self.assertEqual(1, len(outputs))
        output = outputs[0]
        expected = input1 + input2
        np.testing.assert_allclose(expected, output)
