from PIL import Image
import numpy as np
from bayer import bayer
import argparse
import os

if __name__ == "__main__":

		parser = argparse.ArgumentParser(description='Provide image directory')
		parser.add_argument('dir', type=str, help='the path of the image directory')
		args = parser.parse_args()

		input_shape = (64,64)
		output_shape = (60,60)

		for filename in os.listdir(args.dir):
				pic = Image.open(os.path.join(args.dir,filename))
				im = np.array(pic)
				im = np.transpose(im, (2, 0, 1))

				bayer_mosaic = bayer(im)
				# downsample dense gr
				gr = bayer_mosaic[0,...]
				gr_dense = np.zeros(input_shape, dtype=np.int16)
				gr_dense[...] = gr[0:input_shape[0]*2:2, 0:input_shape[1]*2:2]
				gr_dense.tofile("gr.data")

				# downsample dense r
				r = bayer_mosaic[1,...]
				r_dense = np.zeros(input_shape, dtype=np.int16)
				r_dense[...] = r[0:input_shape[0]*2:2, 1:input_shape[1]*2+1:2]
				r_dense.tofile("r.data")

				# downsample dense b
				b = bayer_mosaic[2,...]
				b_dense = np.zeros(input_shape, dtype=np.int16)
				b_dense[...] = b[1:input_shape[0]*2+1:2, 0:input_shape[1]*2:2]
				b_dense.tofile("b.data")

				# downsample dense gb
				gb = bayer_mosaic[3,...]
				gb_dense = np.zeros(input_shape, dtype=np.int16)
				gb_dense[...] = gb[1:input_shape[0]*2+1:2, 1:input_shape[1]*2+1:2]
				gb_dense.tofile("gb.data")

				# get green at blue as the correct output
				gr_m, r_m, b_m, gb_m = bayer(im, return_masks=True)
				# green at blue is image's green channel * blue mask at blue channel
				g_at_b = im[1,...]*b_m[2,...]

				# downsample for dense green at blue
				g_at_b_dense = np.zeros(output_shape, dtype=np.int16)
				g_at_b_dense[...] = g_at_b[1:output_shape[0]*2+1:2, 0:output_shape[1]*2:2]
				g_at_b_dense.tofile("g_at_b_dense.data")
				g_at_b_dense_img = Image.fromarray(g_at_b_dense).convert("L")

				print(im[1,0:10,0:10])

				print(gr_dense[0:5,0:5])
			



