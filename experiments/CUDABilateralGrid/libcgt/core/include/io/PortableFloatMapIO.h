#ifndef PORTABLE_FLOAT_MAP_IO_H
#define PORTABLE_FLOAT_MAP_IO_H

#include <vecmath/MatrixT.h>

class Vector3f;
class QString;

class PortableFloatMapIO
{
public:

	// TODO: scale specifies both the endianness and a scale factor

	// if yAxisPointsUp, then the OUTPUT array is written such that
	// the bottom row comes first
	static bool PortableFloatMapIO::read( QString filename,
		bool yAxisPointsUp,
		float** pixels,
		int* piWidth, int* piHeight,
		int* pnComponents,
		float* pfScale );

	// scale must be positive, by default, scale is 1
	// if yAxisPointsUp, then afLuminance is interpreted as bottom row comes first
	// littleEndian = true for writing output as little endian
	// littleEndian = false for writing big endian
	static bool writeGreyscale( QString filename,
		float* afLuminance,
		int width, int height,
		bool yAxisPointsUp = false,
		float scale = 1.f,
		bool littleEndian = true );	

	// scale > 0 is big endian, < 0 is little endian
	static bool writeRGB( QString filename,
		Vector3f* avRGB,
		int width, int height,
		float scale = -1.0f );

	// scale > 0 is big endian, < 0 is little endian
	// set yAxisPointsUp to true if the *input array* is in OpenGL order
	// Note: this function outputs the PFM in the correct up/down orientation
	// the PFM specification clearly states that the samples are written in
	// western reading order.  The popular HDRShop utility likes to flip images
	// upside down.
	static bool writeRGB( QString filename,
		float* afRGBArray,
		int width, int height,
		float scale = -1.0f,
		bool yAxisPointsUp = false );
};

#endif // PORTABLE_FLOAT_MAP_IO_H
