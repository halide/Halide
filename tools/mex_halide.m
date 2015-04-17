function pipeline = mex_halide( generator_filename, varargin )
%mex_halide - Create a mex library from a Halide generator source file.
% generator_filename identifies a C++ source file containing a generator.
% The remaining arguments are a list of name-value pairs of the form
% 'generator_param=value' used to assign the generator params, or
% additional flags:
%   -s <file>: Add another source file to the Generator build.
%   -e assembly,bitcode,stmt,html: Which outputs to emit from the generator.
%   -c <compiler>: Which C++ compiler to use to build the generator.

    gengen_cpp = ['#include "Halide.h"', sprintf('\n'), ...
                  'int main(int argc, char **argv) {', ...
                  '  return Halide::Internal::generate_filter_main(argc, argv, std::cerr);', ...
                  '}'];

    % Make a temporary directory for our intermediates.
    temp = fullfile(tempdir, 'mex_halide');
    mkdir(temp);

    % Write the generator main program to a temporary file.
    gengen_filename = fullfile(temp, 'GenGen.cpp');
    gengen_file = fopen(gengen_filename, 'w');
    fprintf(gengen_file, '%s', gengen_cpp);
    fclose(gengen_file);

    % Build the filenames of the intermediate object we will generate.
    [path, filename] = fileparts(generator_filename);
    object_file = fullfile(temp, [filename, '.o']);
    function_name = filename;

    % Concatenate the generator args into a single string.
    generator_args = strjoin(varargin);
    target = 'host-matlab';

    if isempty(getenv('HALIDE_PATH'))
        % If the user hasnt set the halide path, assume its the current
        % directory.
        setenv('HALIDE_PATH', pwd);
        warning('HALIDE_PATH environment variable is unset, assuming current directory.');
    end
    halide_path = getenv('HALIDE_PATH');

    libhalide = fullfile(halide_path, 'bin', 'libHalide.so');

    if isempty(getenv('HALIDE_CXX'))
        % If the user hasnt set a compiler for Halide, use g++.
        setenv('HALIDE_CXX', 'g++');
    end
    halide_cxx = getenv('HALIDE_CXX');

    ld_library_path = fullfile(halide_path, 'bin');

    % Build the command to build the generator.
    gen_bin = fullfile(temp, [function_name, '.generator']);
    build_generator = ...
        [halide_cxx, ...
         ' -g -std=c++11 -fno-rtti -I${HALIDE_PATH}/include ', ...
         ' -lz -lpthread -ldl ', ...
         libhalide, ' ', ...
         gengen_filename, ' ', ...
         generator_filename, ' ', ...
         '-o ', gen_bin];
    status = system(build_generator);
    if status ~= 0
        return;
    end

    % Run the generator to build the object file.
    build_object = ...
        ['LD_LIBRARY_PATH=', ld_library_path, ' ', ...
         'DYLD_LIBRARY_PATH=', ld_library_path, ' ', ...
         gen_bin, ' ', ...
         '-f ', function_name, ' ', ...
         '-o ', temp, ' ', ...
         'target=', target, ' ', ...
         generator_args];
    status = system(build_object);
    if status ~= 0
        return;
    end

    % Run mex on the resulting object file.
    mex(object_file, '-ldl');

    % Get the resulting function.
    pipeline = str2func(filename);

end

