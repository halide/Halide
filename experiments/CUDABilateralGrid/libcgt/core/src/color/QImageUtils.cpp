#if 0

#include "color/QImageUtils.h"

#include "color/ColorUtils.h"
#include "math/Arithmetic.h"
#include "math/MathUtils.h"

// static
void QImageUtils::clearImage( QImage* q )
{
	q->fill( qRgba( 0, 0, 0, 0 ) );
}

// static
Vector4f QImageUtils::sample( QImage* q, float x, float y )
{
	int w = q->width();
	int h = q->height();

	// clamp to edge
	x = MathUtils::clampToRangeFloat( x, 0, w );
	y = MathUtils::clampToRangeFloat( y, 0, h );

	int x0 = MathUtils::clampToRangeInt( Arithmetic::floorToInt( x ), 0, w );
	int x1 = MathUtils::clampToRangeInt( x0 + 1, 0, w );
	int y0 = MathUtils::clampToRangeInt( Arithmetic::floorToInt( y ), 0, h );
	int y1 = MathUtils::clampToRangeInt( y0 + 1, 0, h );

	float xf = x - x0;
	float yf = y - y0;

	Vector4f v00 = QImageUtils::qrgbaToVector4f( q->pixel( x0, y0 ) );
	Vector4f v01 = QImageUtils::qrgbaToVector4f( q->pixel( x0, y1 ) );
	Vector4f v10 = QImageUtils::qrgbaToVector4f( q->pixel( x1, y0 ) );
	Vector4f v11 = QImageUtils::qrgbaToVector4f( q->pixel( x1, y1 ) );

	// x = 0
	Vector4f v0 = Vector4f::lerp( v00, v01, yf );
	Vector4f v1 = Vector4f::lerp( v10, v11, yf );

	return Vector4f::lerp( v0, v1, xf );
}

// static
Vector4f QImageUtils::sample( QImage* q, const Vector2f& xy )
{
	return QImageUtils::sample( q, xy.x(), xy.y() );
}

// static
double QImageUtils::sample( const GMatrixd& a, double i, double j )
{
	int m = a.numRows();
	int n = a.numCols();

	// clamp to edge
	i = MathUtils::clampToRangeDouble( i, 0, m );
	j = MathUtils::clampToRangeDouble( j, 0, n );

	int i0 = MathUtils::clampToRangeInt( Arithmetic::floorToInt( i ), 0, m );
	int i1 = MathUtils::clampToRangeInt( i0 + 1, 0, m );
	int j0 = MathUtils::clampToRangeInt( Arithmetic::floorToInt( j ), 0, n );
	int j1 = MathUtils::clampToRangeInt( j0 + 1, 0, n );

	double i_f = i - i0;
	double j_f = j - j0;

	double v00 = a( i0, j0 );
	double v01 = a( i0, j1 );
	double v10 = a( i1, j0 );
	double v11 = a( i1, j1 );
	
	double v0 = MathUtils::lerp( v00, v01, j_f );
	double v1 = MathUtils::lerp( v10, v11, j_f );

	return MathUtils::lerp( v0, v1, i_f );
}

// static
Vector4f QImageUtils::qrgbaToVector4f( QRgb q )
{
	return Vector4f
	(
		ColorUtils::intToFloat( qRed( q ) ),
		ColorUtils::intToFloat( qGreen( q ) ),
		ColorUtils::intToFloat( qBlue( q ) ),
		ColorUtils::intToFloat( qAlpha( q ) )
	);
}

// static
QRgb QImageUtils::vector4fToQRgba( const Vector4f& v )
{
	int r = ColorUtils::floatToInt( v.x() );
	int g = ColorUtils::floatToInt( v.y() );
	int b = ColorUtils::floatToInt( v.z() );
	int a = ColorUtils::floatToInt( v.w() );

	return qRgba( r, g, b, a );
}

// static
void QImageUtils::convertQImageToRGBArray( QImage q, ReferenceCountedArray< ubyte > rgbArray )
{
	int width = q.width();
	int height = q.height();

	assert( rgbArray.length() == 3 * width * height );

	int index = 0;
	for( int y = height - 1; y >= 0; --y )
	{
		for( int x = 0; x < width; ++x )
		{
			QRgb color = q.pixel( x, y );
			
			rgbArray[ index ] = qRed( color );
			rgbArray[ index + 1 ] = qGreen( color );
			rgbArray[ index + 2 ] = qBlue( color );

			index += 3;
		}
	}
}

// static
void QImageUtils::convertQImageToRGBArray( QImage q, ReferenceCountedArray< float > rgbArray )
{
	int width = q.width();
	int height = q.height();

	assert( rgbArray.length() == 3 * width * height );

	int index = 0;
	for( int y = height - 1; y >= 0; --y )
	{
		for( int x = 0; x < width; ++x )
		{
			QRgb color = q.pixel( x, y );
			
			rgbArray[ index ] = ColorUtils::intToFloat( qRed( color ) );
			rgbArray[ index + 1 ] = ColorUtils::intToFloat( qGreen( color ) );
			rgbArray[ index + 2 ] = ColorUtils::intToFloat( qBlue( color ) );

			index += 3;
		}
	}
}

// static
void QImageUtils::convertQImageToRGBAArray( QImage q, ReferenceCountedArray< ubyte > rgbaArray )
{
	int width = q.width();
	int height = q.height();

	assert( rgbaArray.length() == 4 * width * height );

	int index = 0;
	for( int y = height - 1; y >= 0; --y )
	{
		for( int x = 0; x < q.width(); ++x )
		{
			QRgb color = q.pixel( x, y );
			
			rgbaArray[ index ] = qRed( color );
			rgbaArray[ index + 1 ] = qGreen( color );
			rgbaArray[ index + 2 ] = qBlue( color );
			rgbaArray[ index + 3 ] = qAlpha( color );

			index += 4;
		}
	}
}

// static
void QImageUtils::convertQImageToRGBAArray( QImage q, ReferenceCountedArray< float > rgbaArray )
{
	int width = q.width();
	int height = q.height();

	assert( rgbaArray.length() == 4 * width * height );

	int index = 0;
	for( int y = height - 1; y >= 0; --y )
	{
		for( int x = 0; x < q.width(); ++x )
		{
			QRgb color = q.pixel( x, y );
			
			rgbaArray[ index ] = ColorUtils::intToFloat( qRed( color ) );
			rgbaArray[ index + 1 ] = ColorUtils::intToFloat( qGreen( color ) );
			rgbaArray[ index + 2 ] = ColorUtils::intToFloat( qBlue( color ) );
			rgbaArray[ index + 3 ] = ColorUtils::intToFloat( qAlpha( color ) );

			index += 4;
		}
	}
}

// static
void QImageUtils::convertQImageToGMatrices( QImage q, GMatrixd* r, GMatrixd* g, GMatrixd* b )
{
	// TODO: check sizes
	for( int y = 0; y < q.height(); ++y )
	{
		for( int x = 0; x < q.width(); ++x )
		{
			QRgb color = q.pixel( x, y );
			
			( *r )( y, x ) = ColorUtils::intToFloat( qRed( color ) );
			( *g )( y, x ) = ColorUtils::intToFloat( qGreen( color ) );
			( *b )( y, x ) = ColorUtils::intToFloat( qBlue( color ) );
		}
	}
}

// static
void QImageUtils::convertQImageToLuminance( QImage q, GMatrixd* pLuminanceOut )
{
	int width = q.width();
	int height = q.height();

	GMatrixd imR( height, width );
	GMatrixd imG( height, width );
	GMatrixd imB( height, width );

	QImageUtils::convertQImageToGMatrices( q, &imR, &imG, &imB );
	for( int y = 0; y < height; ++y )
	{
		for( int x = 0; x < width; ++x )
		{
			Vector3f rgb( imR( y, x ), imG( y, x ), imB( y, x ) );
			( *pLuminanceOut )( y, x ) = ColorUtils::rgb2lab( rgb ).x();
		}
	}
}

// static
void QImageUtils::convertRGBArrayToLuminance( UnsignedByteArray rgbArray, int width, int height, GMatrixd* pLuminanceOut )
{
	int index = 0;

	for( int y = 0; y < height; ++y )
	{
		for( int x = 0; x < width; ++x )
		{
			Vector3f rgb( rgbArray[ index ], rgbArray[ index + 1 ], rgbArray[ index + 2 ] );
			( *pLuminanceOut )( height - y - 1, x ) = ColorUtils::rgb2lab( rgb ).x();

			index += 3;
		}
	}
}

// static
void QImageUtils::convertGMatrixdToQImage( const GMatrixd& luminance, QImage* pImage, float alpha )
{
	assert( luminance.numRows() == pImage->height() );
	assert( luminance.numCols() == pImage->width() );

	ubyte forceAlpha;
	if( alpha >= 0 )
	{
		forceAlpha = ColorUtils::floatToUnsignedByte( alpha );
	}

	for( uint y = 0; y < luminance.numRows(); ++y )
	{
		for( uint x = 0; x < luminance.numCols(); ++x )
		{
			double d = luminance( y, x );
			ubyte r = ColorUtils::floatToUnsignedByte( d );

			if( alpha < 0 )
			{
				pImage->setPixel( x, y, qRgba( r, r, r, r ) );
			}
			else
			{				
				pImage->setPixel( x, y, qRgba( r, r, r, forceAlpha ) );
			}
		}
	}
}

// static
void QImageUtils::convertRGBArrayToQImage( ReferenceCountedArray< ubyte > rgbArray, int width, int height,
										  QImage* pImage )
{
	// TODO: check size and format

	int index = 0;
	for( int y = height - 1; y >= 0; --y )
	{
		for( int x = 0; x < width; ++x )
		{
			ubyte r = rgbArray[ index ];
			ubyte g = rgbArray[ index + 1 ];
			ubyte b = rgbArray[ index + 2 ];

			pImage->setPixel( x, y, qRgb( r, g, b ) );
			
			index += 3;
		}
	}
}

// static
void QImageUtils::convertRGBArrayToQImage( ReferenceCountedArray< float > rgbArray, int width, int height,
										  QImage* pImage )
{
	// TODO: check size and format

	int index = 0;
	for( int y = height - 1; y >= 0; --y )
	{
		for( int x = 0; x < width; ++x )
		{
			ubyte r = ColorUtils::floatToUnsignedByte( rgbArray[ index ] );
			ubyte g = ColorUtils::floatToUnsignedByte( rgbArray[ index + 1 ] );
			ubyte b = ColorUtils::floatToUnsignedByte( rgbArray[ index + 2 ] );

			pImage->setPixel( x, y, qRgb( r, g, b ) );
			
			index += 3;
		}
	}
}

// static
void QImageUtils::convertRGBAArrayToQImage( ReferenceCountedArray< ubyte > rgbaArray, int width, int height,
							  QImage* pImage )
{
	// TODO: check size and format

	int index = 0;
	for( int y = height - 1; y >= 0; --y )
	{
		for( int x = 0; x < width; ++x )
		{
			ubyte r = rgbaArray[ index ];
			ubyte g = rgbaArray[ index + 1 ];
			ubyte b = rgbaArray[ index + 2 ];
			ubyte a = rgbaArray[ index + 3 ];

			pImage->setPixel( x, y, qRgba( r, g, b, a ) );
			
			index += 4;
		}
	}
}

// static
void QImageUtils::convertRGBAArrayToQImage( ReferenceCountedArray< float > rgbaArray, int width, int height,
							  QImage* pImage )
{
	// TODO: check size and format

	int index = 0;
	for( int y = height - 1; y >= 0; --y )
	{
		for( int x = 0; x < width; ++x )
		{
			ubyte r = ColorUtils::floatToUnsignedByte( rgbaArray[ index ] );
			ubyte g = ColorUtils::floatToUnsignedByte( rgbaArray[ index + 1 ] );
			ubyte b = ColorUtils::floatToUnsignedByte( rgbaArray[ index + 2 ] );
			ubyte a = ColorUtils::floatToUnsignedByte( rgbaArray[ index + 3 ] );

			pImage->setPixel( x, y, qRgba( r, g, b, a ) );
			
			index += 4;
		}
	}
}

#endif