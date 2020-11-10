CONVOLUTION=$1
# Columns are: schedule C W H N filter_width, filter_height, output_depth,
# input_offset, filter_offset, input_depth, stride, pad_width, pad_height,
# byte_zero, output_multiplier, output_shift, output_offset, output_min,
# output_max

$CONVOLUTION 8 17 17 1 1 1 8 -128 -128 8 1 0 0 0
$CONVOLUTION 8 17 17 1 3 3 8 -128 -128 8 1 1 1 0
$CONVOLUTION 8 17 17 1 3 3 8 -128 -128 8 2 1 1 0
$CONVOLUTION 8 17 17 1 3 3 16 -128 -128 8 1 1 1 0
$CONVOLUTION 8 17 17 1 3 3 16 -128 -140 8 1 1 1 0
$CONVOLUTION 12 17 17 1 3 3 16 -128 -140 12 1 1 1 0
