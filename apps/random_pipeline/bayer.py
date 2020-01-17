""" Taken from mgharbi's work on demosaic net"""

"""Utilities to make a mosaic mask and apply it to an image."""
import numpy as np


__all__ = ["bayer", "xtrans"]


"""Bayer mosaic.

The patterned assumed is::

	GR R
	b GB

Args:
	im (np.array): image to mosaic. Dimensions are [c, h, w]

Returns:
	np.array: mosaicked image 
"""
def bayer(im, return_masks=False):

	# gr r b gb
	# green at red mask
	gr_mask = np.ones_like(im)
	print(gr_mask.shape)
	gr_mask[1, :, 1::2] = 0
	gr_mask[1, 1::2, 0::2] = 0

	# green at blue mask
	gb_mask = np.ones_like(im)
	gb_mask[1, :, 0::2] = 0
	gb_mask[1, 0::2, 1::2] = 0

	# red mask
	r_mask = np.ones_like(im)
	r_mask[0, :, 0::2] = 0
	r_mask[0, 1::2, 1::2] = 0

	# blue maks
	b_mask = np.ones_like(im)
	b_mask[2, 0::2, 0::2] = 0
	b_mask[2, :, 1::2] = 0

	if (return_masks):
		return gr_mask, r_mask, b_mask, gb_mask

	bayer_mosaic = np.zeros((4, im.shape[1], im.shape[2]))

	print("gr")
	print(gr_mask[:,0:4,0:4])
	print("r")
	print(r_mask[:, 0:4, 0:4])
	print("b")
	print(b_mask[:, 0:4,0:4])
	print("gb")
	print(gb_mask[:, 0:4,0:4])
	bayer_mosaic[0, ...] = (im*gr_mask)[1, ...]
	bayer_mosaic[1, ...] = (im*r_mask)[0, ...]
	bayer_mosaic[2, ...] = (im*b_mask)[2, ...]
	bayer_mosaic[3, ...] = (im*gb_mask)[1, ...]

	return bayer_mosaic
