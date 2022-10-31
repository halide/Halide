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


def _as_interleaved(im):
    """If the given buffer is 3-dimensional and appears to be planar layout,
       return a view that is in interleaved form, leaving the input unchanged.
       Otherwise, return the image buffer unchanged.
       Note that this call must be used with care, as the returnee may or may
       not be a copy."""
    mv = memoryview(im)
    if mv.ndim == 3 and not is_interleaved(mv):
        return np.moveaxis(mv, 0, 2)
    else:
        return mv


def _as_planar(im):
    """If the given buffer is 3-dimensional and appears to be interleaved
       layout, return a view that is in planar form, leaving the input
       unchanged. Otherwise, return the image buffer unchanged.
       Note that this call must be used with care, as the returnee may or may
       not be a copy."""
    mv = memoryview(im)
    if is_interleaved(mv):
        return np.moveaxis(mv, 2, 0)
    else:
        return mv


def copy_to_interleaved(im):
    """If the given buffer is 3-dimensional and appears to be planar
       layout, return a copy that is in interleaved form. Otherwise, return
       an unchanged copy of the input. Note that this call will always return
       a copy, leaving the input unchanged."""
    return np.copy(_as_interleaved(im))


def copy_to_planar(im):
    """If the given buffer is 3-dimensional and appears to be interleaved
       layout, return a copy that is in planar form. Otherwise, return
       an unchanged copy of the input. Note that this call will always return
       a copy, leaving the input unchanged."""
    return np.copy(_as_planar(im))


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

    # We can use _as_interleaved() here to save a possible copy; since the
    # caller will never see the possibly-a-copy value, there should be no
    # risk of possibly-different behavior between cases that need converting
    # and cases that don't.
    imageio.imwrite(uri, _as_interleaved(im), format, **kwargs)
