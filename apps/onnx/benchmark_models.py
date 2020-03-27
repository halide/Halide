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
        print('Processing', f)

        print('Preparing inputs and outputs')
        onnx_model = onnx.load(f)
        inputs = get_model_inputs(onnx_model)
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
              '-c -o bin/host-cuda/tmp.o bin/host-cuda/{}_li2018.py.cpp'.format(model_name)
        print(cmd)
        os.system(cmd)
        # Link
        cmd = 'g++ ' + \
              python_cflags + \
              '-shared -o bin/host-cuda/{}_li2018.so bin/host-cuda/tmp.o bin/host-cuda/{}_li2018.o'.format(
                      model_name, model_name)
        print(cmd)
        os.system(cmd)
        from mobilenetv2_li2018 import mobilenetv2_li2018
        num_iter = 20
        beg = time.time_ns()
        for i in range(num_iter):
            mobilenetv2_li2018(*halide_inputs, *halide_outputs)
        end = time.time_ns()
        t = ((end - beg) / (10 ** 9)) / num_iter
        print('time for gradient autoscheduler: {}s'.format(t))

        print('Running the autotuning loop for Anderson2020')
        cmd = ('HL_MACHINE_PARAMS=80,1,1 '
               'SAMPLES_DIR={}_autotuned_samples '
               'HL_PERMIT_FAILED_UNROLL=1 '
               'HL_SHARED_MEMORY_LIMIT=48 '
               'bash ../autoscheduler/autotune_loop.sh '
               'bin/host/onnx_converter.generator '
               '{} '
               'host-cuda '
               '../autoscheduler/gpu.weights '
               '../autoscheduler/bin '
               '0 '
               '0 '
               'model_file_path={}').format(model_name, model_name, f)
        print(cmd)
        os.system(cmd)


if __name__ == '__main__':
    main()
