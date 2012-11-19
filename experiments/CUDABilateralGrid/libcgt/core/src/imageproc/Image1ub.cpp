#include "imageproc/Image1ub.h"

#include <math/Arithmetic.h>
#include <math/MathUtils.h>
#include <color/ColorUtils.h>

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

Image1ub::Image1ub() :

	m_width( 0 ),
	m_height( 0 ),
	m_data( NULL )

{

}

Image1ub::Image1ub( int width, int height, quint8 fill ) :

	m_width( width ),
	m_height( height ),
	m_data( width * height, fill )

{

}

Image1ub::Image1ub( const Vector2i& size, quint8 fill ) :

	m_width( size.x ),
	m_height( size.y ),
	m_data( m_width * m_height, fill )

{

}

Image1ub::Image1ub( const Image1ub& copy ) :

m_width( copy.m_width ),
m_height( copy.m_height ),
m_data( copy.m_data )

{

}

Image1ub::Image1ub( Reference< Image1ub > copy ) :

m_width( copy->m_width ),
m_height( copy->m_height ),
m_data( copy->m_data )

{

}

bool Image1ub::isNull() const
{
	return( m_width <= 0 || m_height <= 0 );
}

int Image1ub::width() const
{
	return m_width;
}

int Image1ub::height() const
{
	return m_height;
}

Vector2i Image1ub::size() const
{
	return Vector2i( m_width, m_height );
}

quint8* Image1ub::pixels()
{
	return m_data.data();
}

quint8 Image1ub::pixel( int x, int y ) const
{
	return m_data[ y * m_width + x ];
}


quint8 Image1ub::pixel( const Vector2i& xy ) const
{
	return pixel( xy.x, xy.y );
}

void Image1ub::setPixel( int x, int y, quint8 pixel )
{
	m_data[ y * m_width + x ] = pixel;
}

void Image1ub::setPixel( const Vector2i& xy, quint8 pixel )
{
	setPixel( xy.x, xy.y, pixel );
}

quint8 Image1ub::bilinearSample( float x, float y ) const
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

	float v00 = ColorUtils::intToFloat( pixel( x0, y0 ) );
	float v01 = ColorUtils::intToFloat( pixel( x0, y1 ) );
	float v10 = ColorUtils::intToFloat( pixel( x1, y0 ) );
	float v11 = ColorUtils::intToFloat( pixel( x1, y1 ) );

	float v0 = MathUtils::lerp( v00, v01, yf ); // x = 0
	float v1 = MathUtils::lerp( v10, v11, yf ); // x = 1

	float vf = MathUtils::lerp( v0, v1, xf );
	return static_cast< quint32 >( ColorUtils::floatToInt( vf ) );
}

QImage Image1ub::toQImage()
{
	QImage q( m_width, m_height, QImage::Format_ARGB32 );

	for( int y = 0; y < m_height; ++y )
	{
		for( int x = 0; x < m_width; ++x )
		{
			quint8 pi = pixel( x, y );
			QRgb rgba = qRgba( pi, pi, pi, 255 );
			q.setPixel( x, m_height - y - 1, rgba );
		}
	}

	return q;
}

void Image1ub::savePNG( QString filename )
{
	toQImage().save( filename, "PNG" );
}
