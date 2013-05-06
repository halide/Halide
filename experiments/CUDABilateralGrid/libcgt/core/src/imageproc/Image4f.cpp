#include "imageproc/Image4f.h"

#include <QFile>
#include <QDataStream>
#include <QTextStream>

#include "color/ColorUtils.h"
#include "math/Arithmetic.h"
#include "math/MathUtils.h"
#include "vecmath/Vector4i.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

Image4f::Image4f() :

	m_width( 0 ),
	m_height( 0 )

{

}

Image4f::Image4f( QString filename ) :

	m_width( 0 ),
	m_height( 0 )

{
	load( filename );
}

Image4f::Image4f( int width, int height, const Vector4f& fill ) :

	m_width( width ),
	m_height( height ),
	m_data( 4 * m_width, m_height )

{
	int nPixels = m_width * m_height;
	for( int i = 0; i < nPixels; ++i )
	{
		m_data[ 4 * i ] = fill.x;
		m_data[ 4 * i + 1 ] = fill.y;
		m_data[ 4 * i + 2 ] = fill.z;
		m_data[ 4 * i + 3 ] = fill.w;
	}
}

Image4f::Image4f( const Vector2i& size, const Vector4f& fill ) :

	m_width( size.x ),
	m_height( size.y ),
	m_data( 4 * m_width, m_height )

{
	int nPixels = m_width * m_height;
	for( int i = 0; i < nPixels; ++i )
	{
		m_data[ 4 * i ] = fill.x;
		m_data[ 4 * i + 1 ] = fill.y;
		m_data[ 4 * i + 2 ] = fill.z;
		m_data[ 4 * i + 3 ] = fill.w;
	}
}

Image4f::Image4f( const Image4f& copy ) :

	m_width( copy.m_width ),
	m_height( copy.m_height ),
	m_data( copy.m_data )

{

}

Image4f::Image4f( Reference< Image4f > copy ) :

	m_width( copy->m_width ),
	m_height( copy->m_height ),
	m_data( copy->m_data )

{

}

bool Image4f::isNull() const
{
	return( m_width <= 0 || m_height <= 0 );
}

int Image4f::width() const
{
	return m_width;
}

int Image4f::height() const
{
	return m_height;
}

Vector2i Image4f::size() const
{
	return Vector2i( m_width, m_height );
}

float* Image4f::pixels()
{
	return m_data.getRowPointer( 0 );
}

Vector4f* Image4f::pixelsVector4f()
{
	return reinterpret_cast< Vector4f* >( pixels() );
}

Vector4f Image4f::pixel( int x, int y ) const
{
	int index = 4 * ( y * m_width + x );
	
	return Vector4f
	(
		m_data[ index ],
		m_data[ index + 1 ],
		m_data[ index + 2 ],
		m_data[ index + 3 ]
	);
}

Vector4f Image4f::pixel( const Vector2i& xy ) const
{
	return pixel( xy.x, xy.y );
}

void Image4f::setPixel( int x, int y, const Vector4f& pixel )
{
	int index = 4 * ( y * m_width + x );

	m_data[ index ] = pixel[ 0 ];
	m_data[ index + 1 ] = pixel[ 1 ];
	m_data[ index + 2 ] = pixel[ 2 ];
	m_data[ index + 3 ] = pixel[ 3 ];
}

void Image4f::setPixel( const Vector2i& xy, const Vector4f& pixel )
{
	setPixel( xy.x, xy.y, pixel );
}

void Image4f::setPixel( int x, int y, const Vector4i& pixel )
{
	Vector4f v = ColorUtils::intToFloat( pixel );
	setPixel( x, y, v );
}

void Image4f::setPixel( const Vector2i& xy, const Vector4i& pixel )
{
	setPixel( xy.x, xy.y, pixel );
}

Vector4f Image4f::operator () ( int x, int y ) const
{
	int index = 4 * ( y * m_width + x );
	return Vector4f
	(
		m_data[ index ],
		m_data[ index + 1 ],
		m_data[ index + 2 ],
		m_data[ index + 3 ]
	);
}

Vector4f Image4f::operator () ( const Vector2i& xy ) const
{
	int index = 4 * ( xy.y * m_width + xy.x );
	return Vector4f
	(
		m_data[ index ],
		m_data[ index + 1 ],
		m_data[ index + 2 ],
		m_data[ index + 3 ]
	);
}

Image4f Image4f::flipUD() const
{
	// TODO: do memcpy per row
	Image4f output( m_width, m_height );

	for( int y = 0; y < m_height; ++y )
	{
		int yy = m_height - y - 1;
		for( int x = 0; x < m_width; ++x )
		{
			Vector4f p = pixel( x, yy );
			output.setPixel( x, y, p );
		}
	}

	return output;
}

Vector4f Image4f::bilinearSample( float x, float y ) const
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

	Vector4f v00 = pixel( x0, y0 );
	Vector4f v01 = pixel( x0, y1 );
	Vector4f v10 = pixel( x1, y0 );
	Vector4f v11 = pixel( x1, y1 );

	Vector4f v0 = Vector4f::lerp( v00, v01, yf ); // x = 0
	Vector4f v1 = Vector4f::lerp( v10, v11, yf ); // x = 1

	return Vector4f::lerp( v0, v1, xf );
}

Vector4f Image4f::bilinearSample( const Vector2f& xy ) const
{
	return bilinearSample( xy.x, xy.y );
}

QImage Image4f::toQImage()
{
	QImage q( m_width, m_height, QImage::Format_ARGB32 );
	
	for( int y = 0; y < m_height; ++y )
	{
		for( int x = 0; x < m_width; ++x )
		{
			Vector4f pf = pixel( x, y );
			Vector4i pi = ColorUtils::floatToInt( pf );
			QRgb rgba = qRgba( pi[ 0 ], pi[ 1 ], pi[ 2 ], pi[ 3 ] );
			q.setPixel( x, m_height - y - 1, rgba );
		}
	}

	return q;
}

bool Image4f::load( QString filename )
{
	if( filename.endsWith( ".pfm", Qt::CaseInsensitive ) )
	{
		return loadPFM( filename );
	}
	else if( filename.endsWith( ".pfm4", Qt::CaseInsensitive ) )
	{
		return loadPFM4( filename );
	}
	else
	{
		return loadQImage( filename );
	}
}

bool Image4f::save( QString filename )
{
	if( filename.endsWith( ".pfm", Qt::CaseInsensitive ) )
	{
		return savePFM( filename );
	}
	else if( filename.endsWith( ".pfm4", Qt::CaseInsensitive ) )
	{
		return savePFM4( filename );
	}
	else if( filename.endsWith( ".txt", Qt::CaseInsensitive ) )
	{
		return saveTXT( filename );
	}
	else if( filename.endsWith( ".png", Qt::CaseInsensitive ) )
	{
		return savePNG( filename );
	}
	else
	{
		return false;
	}
}

bool Image4f::loadQImage( QString filename )
{
	QImage q( filename );
	if( q.isNull() )
	{
		return false;	
	}

	m_width = q.width();
	m_height = q.height();
	m_data.resize( 4 * m_width, m_height );

	for( int y = 0; y < m_height; ++y )
	{
		for( int x = 0; x < m_width; ++x )
		{
			QRgb p = q.pixel( x, m_height - y - 1 );
			Vector4i vi( qRed( p ), qGreen( p ), qBlue( p ), qAlpha( p ) );
			Vector4f vf = ColorUtils::intToFloat( vi );
			setPixel( x, y, vf );
		}
	}

	return true;
}

bool Image4f::loadPFM( QString filename )
{
	QFile inputFile( filename );

	// try to open the file in read only mode
	if( !( inputFile.open( QIODevice::ReadOnly ) ) )
	{		
		return false;
	}

	QTextStream inputTextStream( &inputFile );
	inputTextStream.setCodec( "ISO-8859-1" );

	// read header
	QString qsType;
	QString qsWidth;
	QString qsHeight;
	QString qsScale;	

	inputTextStream >> qsType;
	if( qsType != "PF" )
	{
		inputFile.close();
		return false;
	}

	inputTextStream >> qsWidth >> qsHeight >> qsScale;

	int width = qsWidth.toInt();
	int height = qsHeight.toInt();
	float scale = qsScale.toFloat();

	if( width < 0 || height < 0 || scale >= 0 )
	{
		inputFile.close();
		return false;
	}

	// close the text stream
	inputTextStream.setDevice( NULL );
	inputFile.close();

	// now reopen it again in binary mode
	if( !( inputFile.open( QIODevice::ReadOnly ) ) )
	{
		return false;
	}

	int headerLength = qsType.length() + qsWidth.length() + qsHeight.length() + qsScale.length() + 4;	

	QDataStream inputDataStream( &inputFile );
	inputDataStream.skipRawData( headerLength );

	Array2D< float > data( 4 * width * height, 1.f );
	int status;

	for( int y = 0; y < height; ++y )
	{
		for( int x = 0; x < width; ++x )
		{
			int yy = height - y - 1;

			char* bufferPointer = reinterpret_cast< char* >( &( data[ 4 * ( yy * width + x ) ] ) );
			status = inputDataStream.readRawData( bufferPointer, 3 * sizeof( float ) );
		}
	}

	inputFile.close();

	m_width = width;
	m_height = height;
	m_data = data;
	return true;
}

// static
bool Image4f::loadPFM4( QString filename )
{
	QFile inputFile( filename );

	// try to open the file in read only mode
	if( !( inputFile.open( QIODevice::ReadOnly ) ) )
	{		
		return false;
	}

	QTextStream inputTextStream( &inputFile );
	inputTextStream.setCodec( "ISO-8859-1" );

	// read header
	QString qsType;
	QString qsWidth;
	QString qsHeight;
	QString qsScale;	

	inputTextStream >> qsType;
	if( qsType != "PF4" )
	{
		inputFile.close();
		return false;
	}

	inputTextStream >> qsWidth >> qsHeight >> qsScale;

	int width = qsWidth.toInt();
	int height = qsHeight.toInt();
	float scale = qsScale.toFloat();

	if( width < 0 || height < 0 || scale >= 0 )
	{
		inputFile.close();
		return false;
	}

	// close the text stream
	inputTextStream.setDevice( NULL );
	inputFile.close();

	// now reopen it again in binary mode
	if( !( inputFile.open( QIODevice::ReadOnly ) ) )
	{
		return false;
	}

	int headerLength = qsType.length() + qsWidth.length() + qsHeight.length() + qsScale.length() + 4;	

	QDataStream inputDataStream( &inputFile );
	inputDataStream.skipRawData( headerLength );

	Array2D< float > data( 4 * width, height );
	int status;

	for( int y = 0; y < height; ++y )
	{
		int yy = height - y - 1;

		char* rowPointer = reinterpret_cast< char* >( &( data[ 4 * yy * width ] ) );
		status = inputDataStream.readRawData( rowPointer, 4 * width * sizeof( float ) );
	}

	inputFile.close();
	m_width = width;
	m_height = height;
	m_data = data;
	return true;
}

bool Image4f::savePNG( QString filename )
{
	return toQImage().save( filename, "PNG" );
}

bool Image4f::saveTXT( QString filename )
{
	QFile outputFile( filename );

	// try to open the file in write only mode
	if( !( outputFile.open( QIODevice::WriteOnly ) ) )
	{
		return false;
	}

	QTextStream outputTextStream( &outputFile );
	outputTextStream.setCodec( "UTF-8" );

	outputTextStream << "float4 image: width = " << m_width << ", height = " << m_height << "\n";
	outputTextStream << "[index] (x,y_dx) ((x,y_gl)): r g b a\n";

	int k = 0;
	for( int y = 0; y < m_height; ++y )
	{
		int yy = m_height - y - 1;

		for( int x = 0; x < m_width; ++x )
		{
			float r = m_data[ 4 * k ];
			float g = m_data[ 4 * k + 1 ];
			float b = m_data[ 4 * k + 2 ];
			float a = m_data[ 4 * k + 3 ];

			outputTextStream << "[" << k << "] (" << x << "," << y << ") ((" << x << "," << yy << ")): "
				<< r << " " << g << " " << b << " " << a << "\n";

			++k;
		}
	}

	outputFile.close();
	return true;
}

bool Image4f::savePFM( QString filename )
{
	int w = width();
	int h = height();

	// use "wb" binary mode to ensure that on Windows,
	// newlines in the header are written out as '\n'
	FILE* fp = fopen( qPrintable( filename ), "wb" );
	if( fp == NULL )
	{
		return false;
	}

	// write header
	fprintf( fp, "PF\n%d %d\n-1\n", w, h );

	// write data
	for( int y = 0; y < h; ++y )
	{
		int yy = h - y - 1;
		for( int x = 0; x < w; ++x )
		{
			Vector4f rgba = pixel( x, yy );
			fwrite( &rgba, sizeof( float ), 3, fp );
		}
	}

	fclose( fp );
	return true;
}

bool Image4f::savePFM4( QString filename )
{
	int w = width();
	int h = height();

	// use "wb" binary mode to ensure that on Windows,
	// newlines in the header are written out as '\n'
	FILE* fp = fopen( qPrintable( filename ), "wb" );
	if( fp == NULL )
	{
		return false;
	}

	// write header
	fprintf( fp, "PF4\n%d %d\n-1\n", w, h );

	// write data
	for( int y = 0; y < h; ++y )
	{
		int yy = h - y - 1;
		for( int x = 0; x < w; ++x )
		{
			Vector4f rgba = pixel( x, yy );
			fwrite( &rgba, sizeof( float ), 4, fp );
		}
	}

	fclose( fp );
	return true;
}
