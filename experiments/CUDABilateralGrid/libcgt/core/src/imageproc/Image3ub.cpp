#include "imageproc/Image3ub.h"

#include <math/Arithmetic.h>
#include <math/MathUtils.h>
#include <color/ColorUtils.h>

#include <imageproc/Image4f.h>

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

Image3ub::Image3ub() :

	m_width( 0 ),
	m_height( 0 ),
	m_data( NULL )

{

}

Image3ub::Image3ub( QString filename ) :

	m_width( 0 ),
	m_height( 0 ),
	m_data( NULL )

{
	QImage q( filename );
	if( !( q.isNull() ) )
	{
		m_width = q.width();
		m_height = q.height();
		m_data = QVector< quint8 >( 3 * m_width * m_height );

		for( int y = 0; y < m_height; ++y )
		{
			for( int x = 0; x < m_width; ++x )
			{
				QRgb p = q.pixel( x, m_height - y - 1 );
				Vector3i vi( qRed( p ), qGreen( p ), qBlue( p ) );				
				setPixel( x, y, vi );
			}
		}
	}
}

Image3ub::Image3ub( int width, int height, const Vector3i& fillValue ) :

	m_width( width ),
	m_height( height ),
	m_data( 3 * m_width * m_height, 0 )

{
	fill( fillValue );
}

Image3ub::Image3ub( const Vector2i& size, const Vector3i& fillValue ) :

	m_width( size.x ),
	m_height( size.y ),
	m_data( 3 * m_width * m_height, 0 )

{
	fill( fillValue );
}

Image3ub::Image3ub( const Image3ub& copy ) :

	m_width( copy.m_width ),
	m_height( copy.m_height ),
	m_data( copy.m_data )

{

}

Image3ub::Image3ub( Reference< Image3ub > copy ) :

m_width( copy->m_width ),
m_height( copy->m_height ),
m_data( copy->m_data )

{

}

bool Image3ub::isNull() const
{
	return( m_width <= 0 || m_height <= 0 );
}

int Image3ub::width() const
{
	return m_width;
}

int Image3ub::height() const
{
	return m_height;
}

Vector2i Image3ub::size() const
{
	return Vector2i( m_width, m_height );
}

quint8* Image3ub::pixels()
{
	return m_data.data();
}

Vector3i Image3ub::pixel( int x, int y ) const
{
	x = MathUtils::clampToRangeInt( x, 0, width() );
	y = MathUtils::clampToRangeInt( y, 0, height() );

	int index = 3 * ( y * m_width + x );

	return Vector3i
	(
		m_data[ index ],
		m_data[ index + 1 ],
		m_data[ index + 2 ]
	);
}

Vector3i Image3ub::pixel( const Vector2i& xy ) const
{
	return pixel( xy.x, xy.y );
}

void Image3ub::setPixel( int x, int y, const Vector3i& pixel )
{
	int index = 3 * ( y * m_width + x );

	m_data[ index ] = ColorUtils::saturate( pixel[ 0 ] );
	m_data[ index + 1 ] = ColorUtils::saturate( pixel[ 1 ] );
	m_data[ index + 2 ] = ColorUtils::saturate( pixel[ 2 ] );
}

void Image3ub::setPixel( const Vector2i& xy, const Vector3i& pixel )
{
	setPixel( xy.x, xy.y, pixel );
}

Vector3i Image3ub::bilinearSample( int x, int y ) const
{
	x = x - 0.5f;
	y = y - 0.5f;

	// clamp to edge
	x = MathUtils::clampToRangeFloat( x, 0, m_width );
	y = MathUtils::clampToRangeFloat( y, 0, m_height );

	int x0 = MathUtils::clampToRangeInt( Arithmetic::floorToInt( x ), 0, m_width );
	int x1 = MathUtils::clampToRangeInt( x0 + 1, 0, m_width );
	int y0 = MathUtils::clampToRangeInt( Arithmetic::floorToInt( y ), 0, m_height );
	int y1 = MathUtils::clampToRangeInt( y0 + 1, 0, m_height );

	float xf = x - x0;
	float yf = y - y0;

	Vector3f v00 = ColorUtils::intToFloat( pixel( x0, y0 ) );
	Vector3f v01 = ColorUtils::intToFloat( pixel( x0, y1 ) );
	Vector3f v10 = ColorUtils::intToFloat( pixel( x1, y0 ) );
	Vector3f v11 = ColorUtils::intToFloat( pixel( x1, y1 ) );

	Vector3f v0 = Vector3f::lerp( v00, v01, yf ); // x = 0
	Vector3f v1 = Vector3f::lerp( v10, v11, yf ); // x = 1

	Vector3f vf = Vector3f::lerp( v0, v1, xf );
	return ColorUtils::floatToInt( vf );
}

void Image3ub::fill( const Vector3i& fill )
{
	int nPixels = m_width * m_height;
	for( int i = 0; i < nPixels; i += 3 )
	{
		m_data[ i ] = ColorUtils::saturate( fill.x );
		m_data[ i + 1 ] = ColorUtils::saturate( fill.y );
		m_data[ i + 2 ] = ColorUtils::saturate( fill.z );
	}
}

QImage Image3ub::toQImage()
{
	QImage q( m_width, m_height, QImage::Format_ARGB32 );

	for( int y = 0; y < m_height; ++y )
	{
		for( int x = 0; x < m_width; ++x )
		{
			Vector3i pi = pixel( x, y );
			QRgb rgba = qRgba( pi.x, pi.y, pi.z, 255 );
			q.setPixel( x, m_height - y - 1, rgba );
		}
	}

	return q;
}

void Image3ub::savePNG( QString filename )
{
	toQImage().save( filename, "PNG" );
}
