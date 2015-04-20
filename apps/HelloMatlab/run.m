% Add the path to mex_halide.m.
addpath('../../tools');

% Build the mex library from the blur generator.
mex_halide('iir_blur.cpp');

% Load the input, create an output buffer of equal size.
input = cast(imread('../images/rgb.png'), 'single') / 255;
output = zeros(size(input), 'single');

% The blur filter coefficient.
alpha = 0.1;

% Call the Halide pipeline.
tic;
iir_blur(input, alpha, output);
toc;

% Write the blurred image.
imwrite(cast(output * 255, 'uint8'), 'blurred.png');
