% Build the mex library from the blur generator.
addpath('../../tools');
mex_halide('iir_blur.cpp', '-e html');

% Load the input, create an output buffer of equal size.
input = cast(imread('../images/rgb.png'), 'single') / 255;
output = zeros(size(input), 'single');

% The blur filter coefficient.
alpha = 0.1;

tic; 
iir_blur(input, alpha, output);
toc;

% Write the blurred image.
imwrite(cast(output * 255, 'uint8'), 'blurred.png');
