""" Given a list of prototxt files, compile them to Halide and benchmark them.
"""
import argparse
import os
import sysconfig

python_include_path = sysconfig.get_paths()['include']
python_cflags = '-I' + python_include_path + ' '
python_cflags += sysconfig.get_config_var('CFLAGS') + ' '
python_cflags += '-fPIC '

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('files', nargs='+')
    args = parser.parse_args()
    os.system('mkdir -p bin/host-cuda')

    for f in args.files:
        print('Processing', f)
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

if __name__ == '__main__':
    main()
