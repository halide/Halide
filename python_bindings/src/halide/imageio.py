try:
    import imageio.v2 as imageio
except:
    import imageio
import numpy as np


def is_interleaved(im):
    """If the given buffer is 3-dimensional and appears to have an interleaved
    layout, return True. Otherwise, return False."""

    # Assume that 'interleaved' will only apply to channels <= 4
    mv = memoryview(im)
    return mv.ndim == 3 and mv.strides[2] == 1 and mv.shape[2] in [1, 2, 3, 4]


def copy_to_interleaved(im):
    """If the given buffer is 3-dimensional and appears to be planar
    layout, return a copy that is in interleaved form. Otherwise, return
    an unchanged copy of the input. Note that this call will always return
    a copy, leaving the input unchanged."""
    mv = memoryview(im)
    if mv.ndim == 3 and not is_interleaved(mv):
        # We are presumably planar, in (c, y, x) order; we need (y, x, c) order
        mv = np.moveaxis(mv, 0, 2)
        mv = np.copy(mv, order="F")
        return mv
    else:
        return im


def copy_to_planar(im):
    """If the given buffer is 3-dimensional and appears to be interleaved
    layout, return a copy that is in planar form. Otherwise, return
    an unchanged copy of the input. Note that this call will always return
    a copy, leaving the input unchanged."""
    mv = memoryview(im)
    if is_interleaved(mv):
        # Interleaved will be in (y, x, c) order; we want (c, y, x) order
        # (which hl.Buffer will reverse into x, y, c order)
        mv = np.moveaxis(mv, 2, 0)
        mv = np.copy(mv, order="C")
        return mv
    else:
        return im


def imread(uri, format=None, **kwargs):
    """halide.imageio.imread is a thin wrapper around imagio.imread,
    except that for 3-dimensional images that appear to be interleaved,
    the result is converted to a planar layout before returning."""
    return copy_to_planar(imageio.imread(uri, format, **kwargs))


def imwrite(uri, im, format=None, **kwargs):
    """halide.imageio.imwrite is a thin wrapper around imagio.imwrite,
    except that for 3-dimensional images that appear to be planar,
    a temporary interleaved copy of the input is made, which is used for
    writing."""
    imageio.imwrite(uri, copy_to_interleaved(im), format, **kwargs)
