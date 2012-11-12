#ifndef PORTABLE_PIXEL_MAP_IO_H
#define PORTABLE_PIXEL_MAP_IO_H

#include <common/BasicTypes.h>
#include <vecmath/MatrixT.h>

class Vector3f;
class QString;

class PortablePixelMapIO
{
public:

	// TODO: text vs binary
	// TODO: stride to skip alpha channel

	/*
	// scale > 0 is big endian, < 0 is little endian
	static bool writeRGB( QString filename,
		Vector3f* avRGB,
		int width, int height,
		float scale = -1.0f );
		*/

	// set yAxisPointsUp to true if the *input array* is in OpenGL order	
	static bool writeRGB( QString filename,
		ubyte* aubRGBArray,
		int width, int height,		
		bool yAxisPointsUp = false );

	// set yAxisPointsUp to true if the *input array* is in OpenGL order
	// the float values are clamped to [0,1] and rescaled to [0,255]
	static bool writeRGB( QString filename,
		float* afRGBArray,
		int width, int height,		
		bool yAxisPointsUp = false );
};

#endif // PORTABLE_PIXEL_MAP_IO_H
