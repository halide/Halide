#include <cassert>
#include <cstdio>

#include <GL/glew.h>

#include <QImage>

#include "GL/GLTexture3D.h"
#include "GL/GLUtilities.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
void GLTexture3D::setEnabled( bool bEnabled )
{
	if( bEnabled )
	{
		glEnable( GL_TEXTURE_3D );
	}
	else
	{
		glDisable( GL_TEXTURE_3D );
	}
}

// static
GLTexture3D* GLTexture3D::createUnsignedByte1Texture( int width, int height, int depth, const ubyte* aubData )
{
	GLTexture::GLTextureInternalFormat internalFormat = GLTexture::LUMINANCE_UNSIGNED_BYTE_8;

	GLTexture3D* pTexture = GLTexture3D::createTexture3D( width, height, depth, internalFormat );

	glTexImage3D( GL_TEXTURE_3D, 0, internalFormat, width, height, depth, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, aubData );

	return pTexture;
}

// static
GLTexture3D* GLTexture3D::createUnsignedByte3Texture( int width, int height, int depth, const ubyte* aubData )
{
	GLTexture::GLTextureInternalFormat internalFormat = GLTexture::RGB_UNSIGNED_BYTE_8;

	GLTexture3D* pTexture = GLTexture3D::createTexture3D( width, height, depth, internalFormat );

	glTexImage3D( GL_TEXTURE_3D, 0, internalFormat, width, height, depth, 0, GL_RGB, GL_UNSIGNED_BYTE, aubData );

	return pTexture;
}

// static
GLTexture3D* GLTexture3D::createUnsignedByte4Texture( int width, int height, int depth, const ubyte* aubData )
{
	GLTexture::GLTextureInternalFormat internalFormat = GLTexture::RGBA_UNSIGNED_BYTE_8;

	GLTexture3D* pTexture = GLTexture3D::createTexture3D( width, height, depth, internalFormat );

	glTexImage3D( GL_TEXTURE_3D, 0, internalFormat, width, height, depth, 0, GL_RGBA, GL_UNSIGNED_BYTE, aubData );

	return pTexture;
}

// static
GLTexture3D* GLTexture3D::createFloat1Texture( int width, int height, int depth, int nBitsPerComponent, const float* afData )
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

	GLTexture3D* pTexture = GLTexture3D::createTexture3D( width, height, depth, internalFormat );

	glTexImage3D( GL_TEXTURE_3D, 0, internalFormat, width, height, depth, 0, GL_LUMINANCE, GL_FLOAT, afData );

	return pTexture;
}

// static
GLTexture3D* GLTexture3D::createFloat3Texture( int width, int height, int depth, int nBits, const float* afData )
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

	GLTexture3D* pTexture = GLTexture3D::createTexture3D( width, height, depth, internalFormat );

	glTexImage3D( GL_TEXTURE_3D, 0, internalFormat, width, height, depth, 0, GL_RGB, GL_FLOAT, afData );

	return pTexture;
}

// static
GLTexture3D* GLTexture3D::createFloat4Texture( int width, int height, int depth, int nBits, const float* afData )
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

	GLTexture3D* pTexture = GLTexture3D::createTexture3D( width, height, depth, internalFormat );

	glTexImage3D( GL_TEXTURE_3D, 0, internalFormat, width, height, depth, 0, GL_RGBA, GL_FLOAT, afData );

	return pTexture;
}

void GLTexture3D::setFloat1Data( const float* afData, int xOffset, int yOffset, int zOffset, int width, int height, int depth )
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
	if( depth == 0 )
	{
		depth = m_iDepth;
	}

	glTexImage3D( GL_TEXTURE_3D, 0, getInternalFormat(), width, height, depth, 0, GL_LUMINANCE, GL_FLOAT, afData );
}

void GLTexture3D::setFloat3Data( const float* afData, int xOffset, int yOffset, int zOffset, int width, int height, int depth )
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
	if( depth == 0 )
	{
		depth = m_iDepth;
	}

	glTexImage3D( GL_TEXTURE_3D, 0, getInternalFormat(), width, height, depth, 0, GL_RGB, GL_FLOAT, afData );
}

void GLTexture3D::setFloat4Data( const float* afData, int xOffset, int yOffset, int zOffset, int width, int height, int depth )
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
	if( depth == 0 )
	{
		depth = m_iDepth;
	}

	glTexImage3D( GL_TEXTURE_3D, 0, getInternalFormat(), width, height, depth, 0, GL_RGBA, GL_FLOAT, afData );
}

void GLTexture3D::setUnsignedByte1Data( const GLubyte* aubData,
									   int xOffset, int yOffset, int zOffset,
									   int width, int height, int depth )
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
	if( depth == 0 )
	{
		depth = m_iDepth;
	}

	glTexImage3D( GL_TEXTURE_3D, 0, getInternalFormat(), width, height, depth, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, aubData );
}

void GLTexture3D::setUnsignedByte3Data( const GLubyte* aubData,
									   int xOffset, int yOffset, int zOffset,
									   int width, int height, int depth )
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
	if( depth == 0 )
	{
		depth = m_iDepth;
	}

	glTexImage3D( GL_TEXTURE_3D, 0, getInternalFormat(), width, height, depth, 0, GL_RGB, GL_UNSIGNED_BYTE, aubData );
}

void GLTexture3D::setUnsignedByte4Data( const GLubyte* aubData,
									   int xOffset, int yOffset, int zOffset,
									   int width, int height, int depth )
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
	if( depth == 0 )
	{
		depth = m_iDepth;
	}

	glTexImage3D( GL_TEXTURE_3D, 0, getInternalFormat(), width, height, depth, 0, GL_RGBA, GL_UNSIGNED_BYTE, aubData );
}

int GLTexture3D::getWidth()
{
	return m_iWidth;
}

int GLTexture3D::getHeight()
{
	return m_iHeight;
}

int GLTexture3D::getDepth()
{
	return m_iDepth;
}

// virtual
void GLTexture3D::setAllWrapModes( GLint iMode )
{
	bind();
	glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, iMode );
	glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, iMode );
	glTexParameteri( GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, iMode );
}

// virtual
void GLTexture3D::dumpToCSV( QString filename )
{
	assert( false );
}

// virtual
void GLTexture3D::dumpToTXT( QString filename, GLint level, GLenum format, GLenum type )
{
	float* pixels = new float[ 4 * m_iWidth * m_iHeight * m_iDepth ];
	getFloat4Data( pixels );

	FILE* fp = fopen( qPrintable( filename ), "w" );
	fprintf( fp, "width = %d, height = %d, depth = %d\n", m_iWidth, m_iHeight, m_iDepth );

	for( int z = 0; z < m_iDepth; ++z )
	{
		for( int y = 0; y < m_iHeight; ++y )
		{
			for( int x = 0; x < m_iWidth; ++x )
			{
				int k = 4 * ( z * m_iWidth * m_iHeight + y * m_iWidth + x );

				fprintf( fp, "{%d} [%d] (%d, %d, %d): <%f, %f, %f, %f>\n", k / 4, k, x, y, z, pixels[k], pixels[k+1], pixels[k+2], pixels[k+3] );
				k += 4;
			}
		}
	}

	delete[] pixels;
	fclose( fp );
}

void GLTexture3D::dumpToPNG( QString filename )
{
	ubyte* pixels = new ubyte[ 4 * m_iWidth * m_iHeight * m_iDepth ];
	getUnsignedByte4Data( pixels );

	QImage q( m_iWidth * m_iDepth, m_iHeight, QImage::Format_ARGB32 );	

	// tile across in x
	for( int z = 0; z < m_iDepth; ++z )
	{
		for( int y = 0; y < m_iHeight; ++y )
		{
			for( int x = 0; x < m_iWidth; ++x )
			{
				int yy = m_iHeight - y - 1;
				int k = 4 * ( z * m_iWidth * m_iHeight + y * m_iWidth + x );

				ubyte r = pixels[ k ];
				ubyte g = pixels[ k + 1 ];
				ubyte b = pixels[ k + 2 ];
				ubyte a = pixels[ k + 3 ];

				q.setPixel( z * m_iWidth + x, yy, qRgba( r, g, b, a ) );
			}
		}
	}

	delete[] pixels;
	q.save( filename, "PNG" );
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

// static
GLTexture3D* GLTexture3D::createTexture3D( int width, int height, int depth,
										  GLTexture::GLTextureInternalFormat internalFormat )
{
	GLTexture3D* pTexture = new GLTexture3D( width, height, depth, internalFormat );
	pTexture->setAllWrapModes( GL_CLAMP );
	pTexture->setFilterMode( GLTexture::GLTextureFilterMode_NEAREST, GLTexture::GLTextureFilterMode_NEAREST );
	return pTexture;
}

GLTexture3D::GLTexture3D( int width, int height, int depth,
						 GLTexture::GLTextureInternalFormat internalFormat ) :

	m_iWidth( width ),
	m_iHeight( height ),
	m_iDepth( depth ),

	GLTexture( GL_TEXTURE_3D, internalFormat )
{
	assert( width > 0 );
	assert( height > 0 );
	assert( depth >  0 );
}
