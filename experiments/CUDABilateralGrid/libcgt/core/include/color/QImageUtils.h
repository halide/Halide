#if 0

#ifndef Q_IMAGE_UTILS_H
#define Q_IMAGE_UTILS_H

#include <QImage>

#include "common/ReferenceCountedArray.h"
#include "vecmath/GMatrixd.h"
#include "vecmath/Vector2f.h"
#include "vecmath/Vector4f.h"

class QImageUtils
{
public:

	// reset it to ( 0, 0, 0, 0 )
	static void clearImage( QImage* q );

	// reads q at a non-integer location using bilinear interpolation and edge clamping
	static Vector4f sample( QImage* q, float x, float y );

	// reads q at a non-integer location using bilinear interpolation and edge clamping
	static Vector4f sample( QImage* q, const Vector2f& xy );

	// TODO: move this somewhere non-stupid
	static double sample( const GMatrixd& a, double i, double j );

	static Vector4f qrgbaToVector4f( QRgb q );
	static QRgb vector4fToQRgba( const Vector4f& v );

	// QImages have y axis pointing down (Windows ordering)
	// rgba arrays have the y axis pointing up (OpenGL ordering)
	// GMatrices are in standard matrix order (0,0) is top left, (1,0) is one row down (QImage ordering transposed)

	static void convertQImageToRGBArray( QImage q, UnsignedByteArray rgbArray );
	static void convertQImageToRGBArray( QImage q, FloatArray rgbArray );

	static void convertQImageToRGBAArray( QImage q, UnsignedByteArray rgbaArray );
	static void convertQImageToRGBAArray( QImage q, FloatArray rgbaArray );

	static void convertQImageToGMatrices( QImage q, GMatrixd* r, GMatrixd* g, GMatrixd* b );
	
	// output is the L channel from Lab
	static void convertQImageToLuminance( QImage q, GMatrixd* pLuminanceOut );

	// output is the L channel from Lab
	static void convertRGBArrayToLuminance( UnsignedByteArray rgbArray, int width, int height, GMatrixd* pLuminanceOut );

	// converts a single channel GMatrixd into an RGBA QImage
	// if alpha is < 0 (the default), then alpha is set to the luminance at each pixel
	// otherwise, it's set to the input value
	static void convertGMatrixdToQImage( const GMatrixd& luminance, QImage* pImage, float alpha = -1 );

	static void convertRGBArrayToQImage( UnsignedByteArray rgbArray, int width, int height, QImage* pImage );
	static void convertRGBArrayToQImage( FloatArray rgbArray, int width, int height, QImage* pImage );

	static void convertRGBAArrayToQImage( UnsignedByteArray rgbaArray, int width, int height, QImage* pImage );
	static void convertRGBAArrayToQImage( FloatArray rgbaArray, int width, int height, QImage* pImage );
};

#endif // Q_IMAGE_UTILS_H


#endif