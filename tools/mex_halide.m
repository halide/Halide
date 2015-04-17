function pipeline = mex_halide( generator_filename, varargin )
%mex_halide - Create a mex library from a Halide generator source file.
% generator_filename identifies a C++ source file containing a generator.
% The remaining arguments are a list of name-value pairs of the form 
% 'generator_param=value' used to assign the generator params.

    % Build the filenames of the various intermediates we will generate.
    [path, filename] = fileparts(generator_filename);
    object = [filename, '.o'];
    
    % Concatenate the generator args into a single string.
    generator_args = strjoin(varargin);
    target = 'host-matlab';

    % TODO: This is lame. Find a nice way to parameterize this.
    hl_root = '.';
    
    % Build and run the generator system command.
    build_generator = ...
        ['LD_LIBRARY_PATH=', fullfile(hl_root, 'bin'), ' ', ...
         fullfile(hl_root, 'tools/gengen.sh'), ' ', ...
         '-c g++', ' ', ...
         '-s ', generator_filename, ' ', ...
         '-o ', pwd, ' ', ...
         '-f ', filename, ' ', ...
         'target=', target, ' ', ...
         generator_args];
    status = system(build_generator);
    
    if status ~= 0
        pipeline = '';
        return;
    end

    % Run mex on the resulting object file.
    mex(object, '-ldl')
   
    % Get the resulting function.
    pipeline = str2func(filename);
   
end

