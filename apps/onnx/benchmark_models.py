""" Given a list of prototxt files, compile them to Halide and benchmark them.
"""
import argparse
import os
os.environ['HL_JIT_TARGET'] = 'host-cuda'
import sysconfig
import sys
import onnx
sys.path.append('../../python_bindings/bin') # python binding
import halide as hl
sys.path.append('./bin/host-cuda')
import time
import tensorflow as tf
from tensorflow.core.framework import graph_pb2
import copy
import onnx_tf.backend

tf.compat.v1.disable_eager_execution()

python_include_path = sysconfig.get_paths()['include']
python_cflags = '-I' + python_include_path + ' '
python_cflags += sysconfig.get_config_var('CFLAGS') + ' '
python_cflags += '-fPIC '

def get_model_inputs(onnx_model):
    constants = {}
    for c in onnx_model.graph.initializer:
        constants[c.name] = True
    inputs = []
    for i in onnx_model.graph.input:
        if i.name not in constants:
            inputs.append(i)
    return inputs

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('files', nargs='+')
    args = parser.parse_args()
    os.system('mkdir -p bin/host-cuda')

    for f in args.files:
        print('Processing ', f)
        onnx_model = onnx.load(f)
        inputs = get_model_inputs(onnx_model)
        tf_model = onnx_tf.backend.prepare(onnx_model, 'GPU')
        # Copy the TF graph and replace the Placeholder
        graph_def = tf_model.graph.as_graph_def()
        new_graph_def = graph_pb2.GraphDef()
        for node in graph_def.node:
            print(node.name)
            if node.op == 'Placeholder':
                shape = [d.size for d in (node.attr['shape'].shape.dim)]
                b = tf.constant(0.5, shape=shape, name=node.name)
                new_graph_def.node.extend([b.op.node_def])
            else:
                new_graph_def.node.extend([copy.deepcopy(node)])
        output_name = new_graph_def.node[-1].name
        tf.import_graph_def(new_graph_def)

        with tf.compat.v1.Session() as sess:
            with tf.device('/device:gpu:0'):
                graph = tf.compat.v1.get_default_graph()
                feed_dict = {}
                sess.run(tf.compat.v1.global_variables_initializer())
                outputs = [graph.get_tensor_by_name('import/' + output_name + ':0')]

                run_options = tf.compat.v1.RunOptions(trace_level=tf.compat.v1.RunOptions.NO_TRACE)

                output_values = sess.run(outputs, feed_dict=feed_dict, options=run_options)

                num_iter = 20
                min_time = 1e20
                for i in range(num_iter):
                    beg = time.time_ns()
                    output_values = sess.run(outputs, feed_dict=feed_dict, options=run_options)
                    end = time.time_ns()
                    t = ((end - beg) / (10 ** 9))
                    min_time = min(t, min_time)
                print('{}: TensorFlow: {}s'.format(f, min_time))

        print('Preparing Halide inputs and outputs')
        outputs = onnx_model.graph.output
        halide_inputs = []
        for i in inputs:
            # TODO: check types
            shape = [d.dim_value for d in (i.type.tensor_type.shape.dim)]
            b = hl.Buffer(hl.Float(32), shape)
            b.fill(0.5)
            b.copy_to_device()
            halide_inputs.append(b)
        halide_outputs = []
        for o in outputs:
            # TODO: check types
            shape = [d.dim_value for d in (o.type.tensor_type.shape.dim)]
            b = hl.Buffer(hl.Float(32), shape)
            b.fill(0.0)
            b.copy_to_device()
            halide_outputs.append(b)

        model_name = os.path.splitext(os.path.basename(f))[0]

        # Compile the generators
        print('Compiling with the Li2018 autoscheduler')
        cmd = ('bin/host/onnx_converter.generator '
               '-e object,python_extension '
               '-g onnx_model_generator '
               '-o bin/host-cuda '
               '-f {}_li2018 '
               '-p ../gradient_autoscheduler/bin/libgradient_autoscheduler.so '
               '-s Li2018 '
               'target=host-cuda '
               'auto_schedule=true '
               'model_file_path={}').format(model_name, f)
        print(cmd)
        os.system(cmd)
        # Compile the python extension to an object file
        cmd = 'g++ ' + \
              python_cflags + \
              '-I../../distrib/include ' + \
              '-std=c++11 ' + \
              '-c -o bin/host-cuda/tmp.o bin/host-cuda/{}_li2018.py.cpp'.format(model_name)
        print(cmd)
        os.system(cmd)
        # Link
        cmd = 'g++ ' + \
              python_cflags + \
              '-std=c++11 ' + \
              '-shared -o bin/host-cuda/{}_li2018.so bin/host-cuda/tmp.o bin/host-cuda/{}_li2018.o'.format(
                      model_name, model_name)
        print(cmd)
        os.system(cmd)
        m = __import__(model_name + '_li2018')
        f = m.__dict__[model_name + '_li2018']
        f(*halide_inputs, *halide_outputs)
        num_iter = 20
        min_time = 1e20
        for i in range(num_iter):
            beg = time.time_ns()
            f(*halide_inputs, *halide_outputs)
            end = time.time_ns()
            t = ((end - beg) / (10 ** 9))
            min_time = min(t, min_time)
        print('{}: Gradient autoscheduler: {}s'.format(f, min_time))


if __name__ == '__main__':
    main()
