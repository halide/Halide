HalideTraceViz accepts Halide-generated binary tracing packets from stdin, and
outputs them as raw 8-bit rgba32 pixel values to stdout. You should pipe the
output of HalideTraceViz into a video encoder or player.

E.g. to encode a video:

```
HL_TARGET=host-trace_stores-trace_loads-trace_realizations <command to make
pipeline> && \
HL_TRACE_FILE=/dev/stdout <command to run pipeline> | \
HalideTraceViz -s 1920 1080 -t 10000 <the -f args> | \
avconv -f rawvideo -pix_fmt bgr32 -s 1920x1080 -i /dev/stdin -c:v h264 output.avi
```

To just watch the trace instead of encoding a video replace the last line with
something like:

```
mplayer -demuxer rawvideo -rawvideo w=1920:h=1080:format=rgba:fps=30 -idle -fixed-vo -
```

The arguments to HalideTraceViz specify how to lay out and render the Funcs of
interest. It acts like a stateful drawing API. The following parameters should
be set zero or one times:

`--size width height` The size of the output frames. Defaults to 1920x1080.

`--timestep timestep` How many Halide computations should be covered by each
frame. Defaults to 10000.

`--decay A B` How quickly should the yellow and blue highlights decay over time.
This is a two-stage exponential decay with a knee in it. A controls the rate at
which they decay while a value is in the process of being computed, and B
controls the rate at which they decay over time after the corresponding value
has finished being computed. 1 means never decay, 2 means halve in opacity every
frame, and 256 or larger means instant decay. The default values for A and B are
1 and 2 respectively, which means that the highlight holds while the value is
being computed, and then decays slowly.

`--hold frames` How many frames to output after the end of the trace. Defaults
to 250.

The following parameters can be set once per Func. With the exception of `--label`,
they continue to take effect for all subsequently defined Funcs.

`--min` The minimum value taken on by a Func. Maps to black.

`--max` The maximum value taken on by a Func. Maps to white.

`--rgb dim` Render Funcs as rgb, with the dimension dim indexing the color
channels.

`--gray` Render Funcs as grayscale.

`--blank` Specify that the output occupied by a Func should be set to black on
its end-realization event.

`--no-blank` The opposite of `--blank`. Leaves the Func's values on the screen.
This is the default.

`--zoom factor` Each value of a Func will draw as a factor x factor box in the
output. Fractional values are allowed.

`--load time` Each load from a Func costs the given number of ticks.

`--store time` Each store to a Func costs the given number of ticks.

`--move x y` Sets the position on the screen corresponding to the Func's 0, 0
coordinate.

`--left dx` Moves the currently set position leftward by the given amount.

`--right dx` Moves the currently set position rightward by the given amount.

`--up dy` Moves the currently set position upward by the given amount.

`--down dy` Moves the currently set position downward by the given amount.

`--push` Copies the currently set position onto a stack of positions.

`--pop` Sets the current position to the value most-recently pushed, and removes
it from the stack.

`--strides ...` Specifies the matrix that maps the coordinates of the Func to
screen pixels. Specified column major. For example, `--strides 1 0 0 1 0 0`
specifies that the Func has three dimensions where the first one maps to
screen-space x coordinates, the second one maps to screen-space y coordinates,
and the third one does not affect screen-space coordinates.

`--uninit r g b` Specifies the on-screen color corresponding to uninitialized
memory. Defaults to black.

`--func name` Mark a Func to be visualized. Uses the currently set values of the
parameters above to specify how.

`--label func label n` When the named Func is first touched, the label appears
with its bottom left corner at the current coordinates and fades in over n
frames.
