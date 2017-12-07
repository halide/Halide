function mex_halide( generator_filename, varargin )
%mex_halide - Create a mex library from a Halide generator source
%file.
%
% generator_filename identifies a C++ source file containing a generator.
% The remaining arguments are a list of name-value pairs of the form
% 'generator_param=value' used to assign the generator params, or
% additional flags:
%  -e <assembly,bitcode,stmt,html>: Which outputs to emit from the
%     generator, multiply outputs can be specified with a comma
%     delimited list.
%  -c <compiler>: Which C++ compiler to use to build the
%     generator. Default is 'c++'.
%  -g <generator>: Which generator to build. If only one generator
%     is registered, it will be used by default.
%
% If a target is specified by a generator param with target=..., the
% 'matlab' feature flag must be present.
%
% This script uses two environment variables that can optionally be
% set or changed:
%  - HALIDE_SRC_PATH: The path to the root directory of Halide. If
%    unspecified, this defaults to '..' relative to mex_halide.m.
%  - HALIDE_CXX: The C++ compiler to use to build generators. The
%    default is 'c++'.

    gengen_cpp = ['#include "Halide.h"', sprintf('\n'), ...
                  'int main(int argc, char **argv) {', ...
                  '  return Halide::Internal::generate_filter_main(argc, argv, std::cerr);', ...
                  '}'];

    % Make a temporary directory for our intermediates.
    temp = fullfile(tempdir, 'mex_halide');
    if ~exist(temp, 'dir')
        mkdir(temp);
    end

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

    if isempty(getenv('HALIDE_SRC_PATH'))
        % If the user has not set the halide path, get the path of
        % this file (presumably in $HALIDE_SRC_PATH/tools/) and use
        % that.
        [path, ~] = fileparts(mfilename('fullpath'));
        halide_path = fullfile(path, '..');
        setenv('HALIDE_SRC_PATH', halide_path);
    end
    halide_path = getenv('HALIDE_SRC_PATH');

    libhalide = fullfile(halide_path, 'bin', 'libHalide.so');
    halide_include = fullfile(halide_path, 'include');

    if isempty(getenv('HALIDE_CXX'))
        % If the user has not set a compiler for Halide, use c++.
        setenv('HALIDE_CXX', 'c++');
    end
    halide_cxx = getenv('HALIDE_CXX');

    ld_library_path = fullfile(halide_path, 'bin');

    % Build the command to build the generator.
    gen_bin = fullfile(temp, [function_name, '.generator']);
    build_generator = ...
        [halide_cxx, ...
         ' -g -Wall -std=c++11 -fno-rtti -I', halide_include, ' ', ...
         gengen_filename, ' ', ...
         generator_filename, ' ', ...
         libhalide, ' ', ...
         ' -lz -lpthread -ldl ', ...
         '-o ', gen_bin];
    status = system(build_generator);
    if status ~= 0
        error('mex_halide:build_failed', 'Generator build failed.');
        return;
    end

    % Run the generator to build the object file.
    build_object = ...
        ['LD_LIBRARY_PATH=', ld_library_path, ' ', ...
         'DYLD_LIBRARY_PATH=', ld_library_path, ' ', ...
         gen_bin, ' ', ...
         '-f ', function_name, ' ', ...
         '-o ', temp, ' ', ...
         '-e o,h ', ...
         'target=', target, ' ', ...
         generator_args];
    status = system(build_object);
    if status ~= 0
        error('mex_halide:build_failed', ['Generator failed to build ' ...
                            'pipeline.']);
        return;
    end

    % Run mex on the resulting object file.
    mex(object_file, '-ldl');

end
