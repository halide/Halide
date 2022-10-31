try:
    import imageio.v2 as imageio
except:
    import imageio
import numpy


def is_interleaved(im):
    """If the given ndarray is 3-dimensional and appears to have an interleaved
       layout, return True. Otherwise, return False."""

    # Assume that 'interleaved' will only apply to channels <= 4
    return im.ndim == 3 and im.strides[2] == 1 and im.shape[2] in [1, 2, 3, 4]


def _as_interleaved(im):
    """If the given ndarray is 3-dimensional and appears to be planar layout,
       return a view that is in interleaved form, leaving the input unchanged.
       Otherwise, return the image ndarray unchanged.
       Note that this call must be used with care, as the returnee may or may
       not be a copy."""
    if im.ndim == 3 and not is_interleaved(im):
        return numpy.moveaxis(im, 0, 2)
    else:
        return im


def _as_planar(im):
    """If the given ndarray is 3-dimensional and appears to be interleaved
       layout, return a view that is in planar form, leaving the input
       unchanged. Otherwise, return the image ndarray unchanged.
       Note that this call must be used with care, as the returnee may or may
       not be a copy."""
    if is_interleaved(im):
        return numpy.moveaxis(im, 2, 0)
    else:
        return im


def copy_to_interleaved(im):
    """If the given ndarray is 3-dimensional and appears to be planar
       layout, return a copy that is in interleaved form. Otherwise, return
       an unchanged copy of the input. Note that this call will always return
       a copy, leaving the input unchanged."""
    return _as_interleaved(im).copy()


def copy_to_planar(im):
    """If the given ndarray is 3-dimensional and appears to be interleaved
       layout, return a copy that is in planar form. Otherwise, return
       an unchanged copy of the input. Note that this call will always return
       a copy, leaving the input unchanged."""
    return _as_planar(im).copy()


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
