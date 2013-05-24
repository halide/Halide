#include <cassert>
#include <cstdio>

#include <GL/glew.h>
#include <math/MathUtils.h>

#include "GL/GLTexture1D.h"
#include "GL/GLUtilities.h"

// ========================================
// Public
// ========================================

// static
void GLTexture1D::setEnabled( bool bEnabled )
{
	if( bEnabled )
	{
		glEnable( GL_TEXTURE_1D );
	}
	else
	{
		glDisable( GL_TEXTURE_1D );
	}
}

// static
GLTexture1D* GLTexture1D::createUnsignedByte1Texture( int width, const ubyte* aubData )
{
	GLTexture::GLTextureInternalFormat internalFormat = GLTexture::LUMINANCE_UNSIGNED_BYTE_8;

	GLTexture1D* pTexture = GLTexture1D::createTexture1D( width, internalFormat );

	glTexImage1D( GL_TEXTURE_1D, 0, internalFormat, width, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, aubData );

	return pTexture;
}

// static
GLTexture1D* GLTexture1D::createUnsignedByte3Texture( int width, const ubyte* aubData )
{
	GLTexture::GLTextureInternalFormat internalFormat = GLTexture::RGB_UNSIGNED_BYTE_8;

	GLTexture1D* pTexture = GLTexture1D::createTexture1D( width, internalFormat );

	glTexImage1D( GL_TEXTURE_1D, 0, internalFormat, width, 0, GL_RGB, GL_UNSIGNED_BYTE, aubData );

	return pTexture;
}

// static
GLTexture1D* GLTexture1D::createUnsignedByte4Texture( int width, const ubyte* aubData )
{
	GLTexture::GLTextureInternalFormat internalFormat = GLTexture::RGBA_UNSIGNED_BYTE_8;

	GLTexture1D* pTexture = GLTexture1D::createTexture1D( width, internalFormat );

	glTexImage1D( GL_TEXTURE_1D, 0, internalFormat, width, 0, GL_RGBA, GL_UNSIGNED_BYTE, aubData );

	return pTexture;
}

// static
GLTexture1D* GLTexture1D::createFloat1Texture( int width, int nBitsPerComponent, const float* afData )
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

	GLTexture1D* pTexture = GLTexture1D::createTexture1D( width, internalFormat );

	glTexImage1D( GL_TEXTURE_1D, 0, internalFormat, width,0, GL_LUMINANCE, GL_FLOAT, afData );

	return pTexture;
}

// static
GLTexture1D* GLTexture1D::createFloat3Texture( int width, int nBits, const float* afData )
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

	GLTexture1D* pTexture = GLTexture1D::createTexture1D( width, internalFormat );

	glTexImage1D( GL_TEXTURE_1D, 0, internalFormat, width, 0, GL_RGB, GL_FLOAT, afData );

	return pTexture;
}

// static
GLTexture1D* GLTexture1D::createFloat4Texture( int width, int nBits, const float* afData )
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

	GLTexture1D* pTexture = GLTexture1D::createTexture1D( width, internalFormat );

	glTexImage1D( GL_TEXTURE_1D, 0, internalFormat, width, 0, GL_RGBA, GL_FLOAT, afData );

	return pTexture;
}

void GLTexture1D::setFloat1Data( const float* afData, int xOffset, int width )
{
	bind();
	if( width == 0 )
	{
		width = m_iWidth;
	}

	glTexImage1D( GL_TEXTURE_1D, 0, getInternalFormat(), width, 0, GL_LUMINANCE, GL_FLOAT, afData );
}

void GLTexture1D::setFloat3Data( const float* afData, int xOffset, int width )
{
	bind();
	if( width == 0 )
	{
		width = m_iWidth;
	}

	glTexImage1D( GL_TEXTURE_1D, 0, getInternalFormat(), width, 0, GL_RGB, GL_FLOAT, afData );
}

void GLTexture1D::setFloat4Data( const float* afData, int xOffset, int width )
{
	bind();
	if( width == 0 )
	{
		width = m_iWidth;
	}

	glTexImage1D( GL_TEXTURE_1D, 0, getInternalFormat(), width, 0, GL_RGBA, GL_FLOAT, afData );
}

void GLTexture1D::setUnsignedByte1Data( const GLubyte* aubData,
									   int xOffset, int width )
{
	bind();
	if( width == 0 )
	{
		width = m_iWidth;
	}

	glTexImage1D( GL_TEXTURE_1D, 0, getInternalFormat(), width, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, aubData );
}

void GLTexture1D::setUnsignedByte3Data( const GLubyte* aubData,
									   int xOffset, int width )
{
	bind();
	if( width == 0 )
	{
		width = m_iWidth;
	}

	glTexImage1D( GL_TEXTURE_1D, 0, getInternalFormat(), width, 0, GL_RGB, GL_UNSIGNED_BYTE, aubData );
}

void GLTexture1D::setUnsignedByte4Data( const GLubyte* aubData,
									   int xOffset, int width )
{
	bind();
	if( width == 0 )
	{
		width = m_iWidth;
	}

	glTexImage1D( GL_TEXTURE_1D, 0, getInternalFormat(), width, 0, GL_RGBA, GL_UNSIGNED_BYTE, aubData );
}

int GLTexture1D::getWidth()
{
	return m_iWidth;
}

void GLTexture1D::setAllWrapModes( GLint iMode )
{
	bind();
	glTexParameteri( GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, iMode );
	glTexParameteri( GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, iMode );
}

// virtual
ubyte* GLTexture1D::getUByteData( GLint level )
{
	// TODO: set for other levels
	assert( level == 0 );

	bind();

	GLenum format;
	int nComponents = getNumComponents();
	switch( nComponents )
	{
	case 1:
		format = GL_LUMINANCE;
		break;
	case 3:
		format = GL_RGB;
		break;
	case 4:
		format = GL_RGBA;
		break;
	}

	ubyte* pixels = new ubyte[ nComponents * m_iWidth ];
	glGetTexImage( GL_TEXTURE_1D, level, format, GL_UNSIGNED_BYTE, pixels );
	return pixels;
}

// virtual
float* GLTexture1D::getFloatData( GLint level )
{
	// TODO: set for other levels
	assert( level == 0 );

	bind();

	GLenum format;
	int nComponents = getNumComponents();
	switch( nComponents )
	{
	case 1:
		format = GL_LUMINANCE;
		break;
	case 3:
		format = GL_RGB;
		break;
	case 4:
		format = GL_RGBA;
		break;
	}

	float* pixels = new float[ nComponents * m_iWidth ];
	glGetTexImage( GL_TEXTURE_1D, level, format, GL_FLOAT, pixels );
	return pixels;
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

// static
GLTexture1D* GLTexture1D::createTexture1D( int width, GLTexture::GLTextureInternalFormat internalFormat )
{
	GLTexture1D* pTexture = new GLTexture1D( width, internalFormat );
	pTexture->setAllWrapModes( GL_CLAMP );
	pTexture->setFilterMode( GLTexture::GLTextureFilterMode_NEAREST, GLTexture::GLTextureFilterMode_NEAREST );
	return pTexture;
}

GLTexture1D::GLTexture1D( int width, GLTexture::GLTextureInternalFormat internalFormat ) :

	m_iWidth( width ),

	GLTexture( GL_TEXTURE_1D, internalFormat )	
{
	assert( width > 0 );
}
