#include <cassert>
#include <cstdio>

#include <GL/glew.h>
#include <math/MathUtils.h>
#include <QImage>

#include "GL/GLTextureRectangle.h"
#include "GL/GLUtilities.h"
#include "io/PortableFloatMapIO.h"
#include "io/PortablePixelMapIO.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
void GLTextureRectangle::setEnabled( bool bEnabled )
{
	if( bEnabled )
	{
		glEnable( GL_TEXTURE_RECTANGLE_ARB );
	}
	else
	{
		glDisable( GL_TEXTURE_RECTANGLE_ARB );
	}
}

// static
Reference< GLTextureRectangle > GLTextureRectangle::create( Reference< Image4ub > image )
{
	return GLTextureRectangle::createUnsignedByte4Texture( image->size(), image->pixels() );
}

// static
Reference< GLTextureRectangle > GLTextureRectangle::create( Reference< Image1f > image, int nBitsPerComponent )
{
	return GLTextureRectangle::createFloat1Texture( image->size(), nBitsPerComponent, image->pixels() );
}

// static
Reference< GLTextureRectangle > GLTextureRectangle::create( Reference< Image4f > image, int nBitsPerComponent )
{
	return GLTextureRectangle::createFloat4Texture( image->size(), nBitsPerComponent, image->pixels() );
}

// static
Reference< GLTextureRectangle > GLTextureRectangle::createDepthTexture( int width, int height, int nBits, const float* data )
{
	GLTexture::GLTextureInternalFormat internalFormat;

	switch( nBits )
	{
	case 16:
		internalFormat = GLTexture::DEPTH_COMPONENT_FLOAT_16;
		break;
	case 24:
		internalFormat = GLTexture::DEPTH_COMPONENT_FLOAT_24;
		break;
	case 32:
		internalFormat = GLTexture::DEPTH_COMPONENT_FLOAT_32;
		break;
	default:
		fprintf( stderr, "Depth texture precision must be 16, 24, or 32 bits" );
		assert( false );
		break;
	}	

	Reference< GLTextureRectangle > texture = GLTextureRectangle::createTextureRectangle( width, height, internalFormat );
	
	glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, internalFormat, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, data );
	
	return texture;
}

// static
GLTextureRectangle* GLTextureRectangle::createUnsignedByte1Texture( int width, int height, const ubyte* aubData )
{
	GLTexture::GLTextureInternalFormat internalFormat = GLTexture::LUMINANCE_UNSIGNED_BYTE_8;

	GLTextureRectangle* pTexture = GLTextureRectangle::createTextureRectangle( width, height,
		internalFormat );

	glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, internalFormat, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, aubData );

	return pTexture;
}

// static
Reference< GLTextureRectangle > GLTextureRectangle::createUnsignedByte3Texture( int width, int height, const ubyte* data )
{
	GLTexture::GLTextureInternalFormat internalFormat = GLTexture::RGB_UNSIGNED_BYTE_8;

	GLTextureRectangle* pTexture = GLTextureRectangle::createTextureRectangle( width, height, internalFormat );

	glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, internalFormat, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data );

	return pTexture;
}

// static
Reference< GLTextureRectangle > GLTextureRectangle::createUnsignedByte3Texture( const Vector2i& size, const ubyte* data )
{
	return createUnsignedByte3Texture( size.x(), size.y(), data );
}

// static
Reference< GLTextureRectangle > GLTextureRectangle::createUnsignedByte4Texture( int width, int height, const ubyte* aubData )
{
	GLTexture::GLTextureInternalFormat internalFormat = GLTexture::RGBA_UNSIGNED_BYTE_8;

	Reference< GLTextureRectangle > texture = GLTextureRectangle::createTextureRectangle( width, height, internalFormat );

	glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, internalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, aubData );

	return texture;
}

// static
Reference< GLTextureRectangle > GLTextureRectangle::createUnsignedByte4Texture( const Vector2i& size, const ubyte* data )
{
	return GLTextureRectangle::createUnsignedByte4Texture( size.x(), size.y(), data );
}

// static
Reference< GLTextureRectangle > GLTextureRectangle::createFloat1Texture( int width, int height, int nBitsPerComponent, const float* afData )
{
	GLTexture::GLTextureInternalFormat internalFormat;

	switch( nBitsPerComponent )
	{
	case 16:
		internalFormat = GLTexture::LUMINANCE_FLOAT_16;
		break;
	case 32:
		//internalFormat = GLTexture::LUMINANCE_FLOAT_32;
        internalFormat = GLTexture::RED_FLOAT_32;
		break;
	default:
		fprintf( stderr, "Floating point texture nBits must be 16 or 32 bits!\n" );
		assert( false );
	}

	GLTextureRectangle* pTexture = GLTextureRectangle::createTextureRectangle( width, height, internalFormat );

	glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, internalFormat, width, height, 0, GL_LUMINANCE, GL_FLOAT, afData );

	return pTexture;
}

// static
Reference< GLTextureRectangle > GLTextureRectangle::createFloat1Texture( const Vector2i& size, int nBitsPerComponent, const float* afData )
{
	return createFloat1Texture( size.x(), size.y(), nBitsPerComponent, afData );
}

// static
GLTextureRectangle* GLTextureRectangle::createFloat2Texture( int width, int height, int nBitsPerComponent, const float* afData )
{
	GLTexture::GLTextureInternalFormat internalFormat;

	switch( nBitsPerComponent )
	{
	case 16:
		internalFormat = GLTexture::LUMINANCE_ALPHA_FLOAT_16;
		break;
	case 32:
		internalFormat = GLTexture::LUMINANCE_ALPHA_FLOAT_32;
		break;
	default:
		fprintf( stderr, "Floating point texture nBits must be 16 or 32 bits!\n" );
		assert( false );
	}

	GLTextureRectangle* pTexture = GLTextureRectangle::createTextureRectangle( width, height, internalFormat );

	glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, internalFormat, width, height, 0, GL_LUMINANCE, GL_FLOAT, afData );

	return pTexture;
}

// static
GLTextureRectangle* GLTextureRectangle::createFloat3Texture( int width, int height, int nBits, const float* afData )
{
	GLTexture::GLTextureInternalFormat internalFormat;

	switch( nBits )
	{
	case 16:
		internalFormat = GLTexture::RGB_FLOAT_16;
		break;
	case 32:
		internalFormat = GLTexture::RGB_FLOAT_32;
		break;
	default:
		fprintf( stderr, "Floating point texture nBits must be 16 or 32 bits!\n" );
		assert( false );
	}

	GLTextureRectangle* pTexture = GLTextureRectangle::createTextureRectangle( width, height, internalFormat );

	glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, internalFormat, width, height, 0, GL_RGB, GL_FLOAT, afData );

	return pTexture;
}

// static
Reference< GLTextureRectangle > GLTextureRectangle::createFloat4Texture( int width, int height, int nBits, const float* afData )
{
	GLTexture::GLTextureInternalFormat internalFormat;

	switch( nBits )
	{
	case 16:
		internalFormat = GLTexture::RGBA_FLOAT_16;
		break;
	case 32:
		internalFormat = GLTexture::RGBA_FLOAT_32;
		break;
	default:
		fprintf( stderr, "Floating point texture nBits must be 16 or 32 bits!\n" );
		assert( false );
	}

	GLTextureRectangle* pTexture = GLTextureRectangle::createTextureRectangle( width, height, internalFormat );

	glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, internalFormat, width, height, 0, GL_RGBA, GL_FLOAT, afData );

	return pTexture;
}

// static
Reference< GLTextureRectangle > GLTextureRectangle::createFloat4Texture( const Vector2i& size, int nBitsPerComponent, const float* data )
{
	return GLTextureRectangle::createFloat4Texture( size.x(), size.y(), nBitsPerComponent, data );
}

void GLTextureRectangle::setData( Reference< Image1f > image )
{
	return setFloat1Data( image->pixels() );
}

void GLTextureRectangle::setData( Reference< Image3ub > image )
{
	return setUnsignedByte3Data( image->pixels() );
}

void GLTextureRectangle::setData( Reference< Image4ub > image )
{
	return setUnsignedByte4Data( image->pixels() );
}

void GLTextureRectangle::setData( Reference< Image4f > image )
{
	return setFloat4Data( image->pixels() );
}

Reference< Image3ub > GLTextureRectangle::getImage3ub( Reference< Image3ub > output )
{
	if( output.isNull() )
	{
		output = new Image3ub( size() );
	}

	// TODO: level, etc
	getUnsignedByte3Data( output->pixels() );

	return output;
}

Reference< Image1f > GLTextureRectangle::getImage1f( Reference< Image1f > output )
{
	if( output.isNull() )
	{
		output = new Image1f( size() );
	}

	// TODO: level, etc
	getFloat1Data( output->pixels() );

	return output;
}

Reference< Image4f > GLTextureRectangle::getImage4f( Reference< Image4f > output )
{
	if( output.isNull() )
	{
		output = new Image4f( size() );
	}

	// TODO: level, etc
	getFloat4Data( output->pixels() );

	return output;
}

void GLTextureRectangle::setFloat1Data( const float* data )
{
	bind();
	glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, getInternalFormat(), m_width, m_height, 0, GL_LUMINANCE, GL_FLOAT, data );
}

void GLTextureRectangle::setFloat3Data( const float* data )
{
	bind();
	glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, getInternalFormat(), m_width, m_height, 0, GL_RGBA, GL_FLOAT, data );
}

void GLTextureRectangle::setFloat4Data( const float* data )
{
	bind();
	glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, getInternalFormat(), m_width, m_height, 0, GL_RGBA, GL_FLOAT, data );
}

void GLTextureRectangle::setUnsignedByte1Data( const GLubyte* data )
{
	bind();
	glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, getInternalFormat(), m_width, m_height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, data );
}

void GLTextureRectangle::setUnsignedByte3Data( const GLubyte* data )
{
	bind();
	glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, getInternalFormat(), m_width, m_height, 0, GL_RGB, GL_UNSIGNED_BYTE, data );
}

void GLTextureRectangle::setUnsignedByte4Data( const GLubyte* data )
{
	bind();
	glTexImage2D( GL_TEXTURE_RECTANGLE_ARB, 0, getInternalFormat(), m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data );
}

int GLTextureRectangle::numElements()
{
	return m_width * m_height;
}

int GLTextureRectangle::getWidth()
{
	return m_width;
}

int GLTextureRectangle::getHeight()
{
	return m_height;
}

Vector2i GLTextureRectangle::size()
{
	return Vector2i( m_width, m_height );
}

void GLTextureRectangle::setAllWrapModes( GLint iMode )
{
	bind();
	glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, iMode );
	glTexParameteri( GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, iMode );
}

// virtual
void GLTextureRectangle::dumpToCSV( QString filename )
{
	// TODO: use getData(), use it to get the right formats

	FloatArray pixels( 4 * m_width * m_height );
	getFloat4Data( pixels );

	int k = 0;
	FILE* fp = fopen( qPrintable( filename ), "w" );
	fprintf( fp, "width = %d,height = %d\n", m_width, m_height );
	fprintf( fp, "pixel,byte,x,y,r,g,b,a\n" );

	for( int y = 0; y < m_height; ++y )
	{
		for( int x = 0; x < m_width; ++x )
		{
			fprintf( fp, "%d,%d,%d,%d,%f,%f,%f,%f\n", k / 4, k, x, y, pixels[k], pixels[k+1], pixels[k+2], pixels[k+3] );
			k += 4;
		}
	}

	fclose( fp );
}

// virtual
void GLTextureRectangle::dumpToTXT( QString filename, GLint level, GLenum format, GLenum type )
{
	// TODO: use getData(), use it to get the right formats

	assert( level == 0 );
	assert( format == GL_RGBA );
	assert( type == GL_FLOAT );

	bind();

	FloatArray pixels( 4 * m_width * m_height );

	glGetTexImage( GL_TEXTURE_RECTANGLE_ARB, level, format, type, pixels );

	FILE* fp = fopen( qPrintable( filename ), "w" );
	fprintf( fp, "width = %d, height = %d\n", m_width, m_height );
	fprintf( fp, "{pixel number} [float number] (rasterX, rasterY), ((openglX, openglY)) : <r, g, b, a>\n" );
	for( int y = 0; y < m_height; ++y )
	{
		for( int x = 0; x < m_width; ++x )
		{
			int k = 4 * ( y * m_width + x );

			fprintf( fp, "{%d} [%d] (%d, %d) ((%d,%d)): <%f, %f, %f, %f>\n", k / 4, k, x, m_height - y - 1, x, y, pixels[k], pixels[k+1], pixels[k+2], pixels[k+3] );
			k += 4;
		}
	}

	fclose( fp );
}

void GLTextureRectangle::savePPM( QString filename )
{
	bind();

	UnsignedByteArray pixels( 3 * m_width * m_height );
	getUnsignedByte3Data( pixels );
	PortablePixelMapIO::writeRGB( filename, pixels, m_width, m_height, true );
	delete[] pixels;
}

void GLTextureRectangle::savePNG( QString filename )
{
	UnsignedByteArray pixels( 4 * m_width * m_height );
	getUnsignedByte4Data( pixels );

	QImage q( m_width, m_height, QImage::Format_ARGB32 );
	for( int y = 0; y < m_height; ++y )
	{
		for( int x = 0; x < m_width; ++x )
		{
			int yy = m_height - y - 1;
			int k = 4 * ( yy * m_width + x );

			ubyte r = pixels[ k ];
			ubyte g = pixels[ k + 1 ];
			ubyte b = pixels[ k + 2 ];
			ubyte a = pixels[ k + 3 ];

			q.setPixel( x, y, qRgba( r, g, b, a ) );
		}
	}

	q.save( filename, "PNG" );
}

void GLTextureRectangle::savePFM( QString filename )
{
	FloatArray pixels( 3 * m_width * m_height );
	getFloat3Data( pixels );

	PortableFloatMapIO::writeRGB( filename, pixels, m_width, m_height, -1.f,
		true );
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

// static
GLTextureRectangle* GLTextureRectangle::createTextureRectangle( int width, int height,
										  GLTexture::GLTextureInternalFormat internalFormat )
{
	GLTextureRectangle* pTexture = new GLTextureRectangle( width, height, internalFormat );
	pTexture->setAllWrapModes( GL_CLAMP );
	pTexture->setFilterMode( GLTexture::GLTextureFilterMode_NEAREST, GLTexture::GLTextureFilterMode_NEAREST );
	return pTexture;
}

GLTextureRectangle::GLTextureRectangle( int width, int height,
						 GLTexture::GLTextureInternalFormat internalFormat ) :

GLTexture( GL_TEXTURE_RECTANGLE_ARB, internalFormat ),
m_width( width ),
m_height( height )

{
	assert( width > 0 );
	assert( height > 0 );
}
