% Add the path to mex_halide.m.
addpath(fullfile(getenv('HALIDE_DISTRIB_PATH'), 'tools'));

% Build the mex library from the blur generator.
mex_halide('iir_blur.cpp', '-g', 'IirBlur');

% Load the input, create an output buffer of equal size.
input = cast(imread('../images/rgb.png'), 'single') / 255;
output = zeros(size(input), 'single');

% The blur filter coefficient.
alpha = 0.1;

% Call the Halide pipeline.
for i = 1:10
    tic;
    iir_blur(input, alpha, output);
    toc;
end

% Write the blurred image.
imwrite(cast(output * 255, 'uint8'), 'blurred.png');
