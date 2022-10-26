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


def to_planar(im):
    """If the given ndarray is 3-dimensional and appears to be interleaved
       layout, return a copy that is in planar form. Otherwise, return the
       ndarray unchanged."""
    if is_interleaved(im):
        im = numpy.moveaxis(im, 2, 0).copy()

    return im


def to_interleaved(im):
    """If the given ndarray is 3-dimensional and appears to be planar
       layout, return a copy that is in interleaved form. Otherwise, return the
       ndarray unchanged."""
    if im.ndim == 3 and not is_interleaved(im):
        im = numpy.moveaxis(im, 0, 2).copy()

    return im


def imread(uri, format=None, **kwargs):
    """halide.imageio.imread is a thin wrapper around imagio.imread,
       except that for 3-dimensional images that appear to be interleaved,
       the result is converted to a planar layout before returning."""
    return to_planar(imageio.imread(uri, format, **kwargs))


def imwrite(uri, im, format=None, **kwargs):
    """halide.imageio.imwrite is a thin wrapper around imagio.imwrite,
       except that for 3-dimensional images that appear to be planar,
       the image has a temporary interleaved copy made, which is used for saving."""
    imageio.imwrite(uri, to_interleaved(im), format, **kwargs)
