#include <cassert>
#include <cstdio>

#include <GL/glew.h>
#include <math/MathUtils.h>
#include <QFile>
#include <QImage>

#include "GL/GLTexture2D.h"
#include "GL/GLUtilities.h"
#include "io/PortableFloatMapIO.h"
#include "io/PortablePixelMapIO.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
void GLTexture2D::setEnabled( bool bEnabled )
{
	if( bEnabled )
	{
		glEnable( GL_TEXTURE_2D );
	}
	else
	{
		glDisable( GL_TEXTURE_2D );
	}
}

// static
GLTexture2D* GLTexture2D::createDepthTexture( int width, int height, int nBits, const uint* auiData )
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

	GLTexture2D* pTexture = GLTexture2D::createTexture2D( width, height, internalFormat );

	glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, auiData );

	return pTexture;
}

// static
GLTexture2D* GLTexture2D::createUnsignedByte1Texture( int width, int height, const ubyte* aubData )
{
	GLTexture::GLTextureInternalFormat internalFormat = GLTexture::LUMINANCE_UNSIGNED_BYTE_8;

	GLTexture2D* pTexture = GLTexture2D::createTexture2D( width, height,
		internalFormat );

	glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, aubData );

	return pTexture;
}

// static
GLTexture2D* GLTexture2D::createUnsignedByte3Texture( int width, int height, const ubyte* aubData )
{
	GLTexture::GLTextureInternalFormat internalFormat = GLTexture::RGB_UNSIGNED_BYTE_8;

	GLTexture2D* pTexture = GLTexture2D::createTexture2D( width, height, internalFormat );

	glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, aubData );

	return pTexture;
}

// static
GLTexture2D* GLTexture2D::createUnsignedByte4Texture( int width, int height, const ubyte* aubData )
{
	GLTexture::GLTextureInternalFormat internalFormat = GLTexture::RGBA_UNSIGNED_BYTE_8;

	GLTexture2D* pTexture = GLTexture2D::createTexture2D( width, height, internalFormat );

	glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, aubData );

	return pTexture;
}

// static
GLTexture2D* GLTexture2D::createFloat1Texture( int width, int height, int nBitsPerComponent, const float* afData )
{
	GLTexture::GLTextureInternalFormat internalFormat;

	switch( nBitsPerComponent )
	{
	case 16:
		internalFormat = GLTexture::LUMINANCE_FLOAT_16;
		break;
	case 32:
		internalFormat = GLTexture::LUMINANCE_FLOAT_32;
		break;
	default:
		fprintf( stderr, "Floating point texture nBits must be 16 or 32 bits!\n" );
		assert( false );
	}

	GLTexture2D* pTexture = GLTexture2D::createTexture2D( width, height, internalFormat );

	glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_LUMINANCE, GL_FLOAT, afData );

	return pTexture;
}

// static
GLTexture2D* GLTexture2D::createFloat3Texture( int width, int height, int nBits, const float* afData )
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

	GLTexture2D* pTexture = GLTexture2D::createTexture2D( width, height, internalFormat );

	glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGB, GL_FLOAT, afData );

	return pTexture;
}

// static
GLTexture2D* GLTexture2D::createFloat4Texture( int width, int height, int nBits, const float* afData )
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

	GLTexture2D* pTexture = GLTexture2D::createTexture2D( width, height, internalFormat );
	
	glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA, GL_FLOAT, afData );

	return pTexture;
}

void GLTexture2D::setFloat1Data( const float* afData, int xOffset, int yOffset, int width, int height )
{
	bind();
	if( width == 0 )
	{
		width = m_iWidth;
	}
	if( height == 0 )
	{
		height = m_iHeight;
	}

	glTexImage2D( GL_TEXTURE_2D, 0, getInternalFormat(), width, height, 0, GL_LUMINANCE, GL_FLOAT, afData );
}

void GLTexture2D::setFloat3Data( const float* afData, int xOffset, int yOffset, int width, int height )
{
	bind();
	if( width == 0 )
	{
		width = m_iWidth;
	}
	if( height == 0 )
	{
		height = m_iHeight;
	}

	glTexImage2D( GL_TEXTURE_2D, 0, getInternalFormat(), width, height, 0, GL_RGB, GL_FLOAT, afData );
}

void GLTexture2D::setFloat4Data( const float* afData, int xOffset, int yOffset, int width, int height )
{
	bind();
	if( width == 0 )
	{
		width = m_iWidth;
	}
	if( height == 0 )
	{
		height = m_iHeight;
	}

	glTexImage2D( GL_TEXTURE_2D, 0, getInternalFormat(), width, height, 0, GL_RGBA, GL_FLOAT, afData );
}

void GLTexture2D::setUnsignedByte1Data( const GLubyte* aubData,
									   int xOffset, int yOffset,
									   int width, int height )
{
	bind();

	if( width == 0 )
	{
		width = m_iWidth;
	}
	if( height == 0 )
	{
		height = m_iHeight;
	}

	glTexImage2D( GL_TEXTURE_2D, 0, getInternalFormat(), width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, aubData );
}

void GLTexture2D::setUnsignedByte3Data( const GLubyte* aubData,
									   int xOffset, int yOffset,
									   int width, int height )
{
	bind();

	if( width == 0 )
	{
		width = m_iWidth;
	}
	if( height == 0 )
	{
		height = m_iHeight;
	}

	glTexImage2D( GL_TEXTURE_2D, 0, getInternalFormat(), width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, aubData );
}

void GLTexture2D::setUnsignedByte4Data( const GLubyte* aubData,
									   int xOffset, int yOffset,
									   int width, int height )
{
	bind();

	if( width == 0 )
	{
		width = m_iWidth;
	}
	if( height == 0 )
	{
		height = m_iHeight;
	}

	glTexImage2D( GL_TEXTURE_2D, 0, getInternalFormat(), width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, aubData );
}

int GLTexture2D::getWidth()
{
	return m_iWidth;
}

int GLTexture2D::getHeight()
{
	return m_iHeight;
}

void GLTexture2D::setAllWrapModes( GLint iMode )
{
	bind();
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, iMode );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, iMode );
}

// virtual
void GLTexture2D::dumpToCSV( QString filename )
{
	// TODO: use getData(), use it to get the right formats

	float* pixels = new float[ 4 * m_iWidth * m_iHeight ];
	getFloat4Data( pixels );

	int k = 0;
	FILE* fp = fopen( qPrintable( filename ), "w" );
	fprintf( fp, "width = %d,height = %d\n", m_iWidth, m_iHeight );
	fprintf( fp, "pixel,byte,x,y,r,g,b,a\n" );

	for( int y = 0; y < m_iHeight; ++y )
	{
		for( int x = 0; x < m_iWidth; ++x )
		{
			fprintf( fp, "%d,%d,%d,%d,%f,%f,%f,%f\n", k / 4, k, x, y, pixels[k], pixels[k+1], pixels[k+2], pixels[k+3] );
			k += 4;
		}
	}

	fclose( fp );

	delete[] pixels;
}

// virtual
void GLTexture2D::dumpToTXT( QString filename, GLint level, GLenum format, GLenum type )
{
	// TODO: use getData(), use it to get the right formats

	assert( level == 0 );
	assert( format == GL_RGBA );
	assert( type == GL_FLOAT );

	float* pixels = new float[ 4 * m_iWidth * m_iHeight ];
	getFloat4Data( pixels );

	int k = 0;
	FILE* fp = fopen( qPrintable( filename ), "w" );
	fprintf( fp, "width = %d, height = %d\n", m_iWidth, m_iHeight );

	for( int y = 0; y < m_iHeight; ++y )
	{
		for( int x = 0; x < m_iWidth; ++x )
		{
			fprintf( fp, "{%d} [%d] (%d, %d): <%f, %f, %f, %f>\n", k / 4, k, x, y, pixels[k], pixels[k+1], pixels[k+2], pixels[k+3] );
			k += 4;
		}
	}

	fclose( fp );

	delete[] pixels;
}

void GLTexture2D::dumpToPPM( QString filename )
{
	ubyte* pixels = new ubyte[ 3 * m_iWidth * m_iHeight ];
	getUnsignedByte3Data( pixels );
	PortablePixelMapIO::writeRGB( filename, pixels, m_iWidth, m_iHeight, true );
	delete[] pixels;
}

void GLTexture2D::dumpToPNG( QString filename )
{
	ubyte* pixels = new ubyte[ 4 * m_iWidth * m_iHeight ];
	getUnsignedByte4Data( pixels );
	
	// TODO: arrayToQImage
	QImage q( m_iWidth, m_iHeight, QImage::Format_ARGB32 );
	for( int y = 0; y < m_iHeight; ++y )
	{
		for( int x = 0; x < m_iWidth; ++x )
		{
			int yy = m_iHeight - y - 1;
			int k = 4 * ( yy * m_iWidth + x );

			ubyte r = pixels[ k ];
			ubyte g = pixels[ k + 1 ];
			ubyte b = pixels[ k + 2 ];
			ubyte a = pixels[ k + 3 ];

			q.setPixel( x, y, qRgba( r, g, b, a ) );
		}
	}

	delete[] pixels;
	q.save( filename, "PNG" );
}

void GLTexture2D::dumpToPFM( QString filename )
{
	float* pixels = new float[ 3 * m_iWidth * m_iHeight ];
	getFloat3Data( pixels );

	PortableFloatMapIO::writeRGB( filename, pixels, m_iWidth, m_iHeight, -1.f,
		true );

	delete[] pixels;
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

// static
GLTexture2D* GLTexture2D::createTexture2D( int width, int height,
										  GLTexture::GLTextureInternalFormat internalFormat )
{
	GLTexture2D* pTexture = new GLTexture2D( width, height, internalFormat );
	pTexture->setAllWrapModes( GL_CLAMP );
	pTexture->setFilterMode( GLTexture::GLTextureFilterMode_NEAREST, GLTexture::GLTextureFilterMode_NEAREST );
	return pTexture;
}

GLTexture2D::GLTexture2D( int width, int height,
						 GLTexture::GLTextureInternalFormat internalFormat ) :

	m_iWidth( width ),
	m_iHeight( height ),

	GLTexture( GL_TEXTURE_2D, internalFormat )	
{
	assert( width > 0 );
	assert( height > 0 );
}
