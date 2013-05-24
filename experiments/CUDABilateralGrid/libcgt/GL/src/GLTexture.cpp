#include "GL/GLTexture.h"

#include <cstdio>

#include "GL/GLUtilities.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
int GLTexture::getMaxTextureSize()
{
	int maxSize;
	glGetIntegerv( GL_MAX_TEXTURE_SIZE, &maxSize );
	return maxSize;
}

// static
GLfloat GLTexture::getLargestSupportedAnisotropy()
{
	GLfloat largestSupportedAnisotropy;
	glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &largestSupportedAnisotropy );
	return largestSupportedAnisotropy;
}

// virtual
GLTexture::~GLTexture()
{
	unbind();
	glDeleteTextures( 1, &m_iTextureId );
}

void GLTexture::bind()
{
	glBindTexture( m_eTarget, m_iTextureId );
}

void GLTexture::unbind()
{
	glBindTexture( m_eTarget, 0 );
}

GLuint GLTexture::getTextureId()
{
	return m_iTextureId;
}

GLenum GLTexture::getTarget()
{
	return m_eTarget;
}

GLTexture::GLTextureInternalFormat GLTexture::getInternalFormat()
{
	return m_eInternalFormat;
}

int GLTexture::getNumComponents()
{
	return m_nComponents;
}

int GLTexture::getNumBitsPerComponent()
{
	return m_nBitsPerComponent;
}

GLTexture::GLTextureFilterMode GLTexture::getMinFilterMode()
{
	bind();

	GLint mode;

	glGetTexParameteriv( m_eTarget, GL_TEXTURE_MIN_FILTER, &mode );

	return( ( GLTexture::GLTextureFilterMode )mode );
}

GLTexture::GLTextureFilterMode GLTexture::getMagFilterMode()
{
	bind();

	GLint mode;

	glGetTexParameteriv( m_eTarget, GL_TEXTURE_MAG_FILTER, &mode );

	return( ( GLTexture::GLTextureFilterMode )mode );
}

void GLTexture::setFilterModeNearest()
{
	setFilterMode( GLTexture::GLTextureFilterMode_NEAREST );
}

void GLTexture::setFilterModeLinear()
{
	setFilterMode( GLTexture::GLTextureFilterMode_LINEAR );
}

void GLTexture::setFilterMode( GLTexture::GLTextureFilterMode minAndMagMode )
{
	setFilterMode( minAndMagMode, minAndMagMode );
}

void GLTexture::setFilterMode( GLTexture::GLTextureFilterMode minFilterMode, GLTexture::GLTextureFilterMode magFilterMode )
{
	bind();

	glTexParameteri( m_eTarget, GL_TEXTURE_MIN_FILTER, static_cast< GLint >( minFilterMode ) );
	glTexParameteri( m_eTarget, GL_TEXTURE_MAG_FILTER, static_cast< GLint >( magFilterMode ) );
}

void GLTexture::setAnisotropicFiltering( GLfloat anisotropy )
{
	bind();
	glTexParameterf( m_eTarget, GL_TEXTURE_MAX_ANISOTROPY_EXT, anisotropy );
}

void GLTexture::setWrapMode( GLenum eParam, GLint iMode )
{
	bind();
	glTexParameteri( m_eTarget, eParam, iMode );
}

// virtual
void GLTexture::getFloat1Data( float* afOutput, int level )
{
	GLenum format = GL_LUMINANCE;
	getTexImage( level, format, GL_FLOAT, afOutput );
}

// virtual
void GLTexture::getFloat3Data( float* afOutput, int level )
{
	GLenum format = GL_RGB;
	getTexImage( level, format, GL_FLOAT, afOutput );
}

// virtual
void GLTexture::getFloat4Data( float* afOutput, int level )
{
	GLenum format = GL_RGBA;
	getTexImage( level, format, GL_FLOAT, afOutput );
}

// virtual
void GLTexture::getUnsignedByte1Data( ubyte* aubOutput, int level )
{
	GLenum format = GL_LUMINANCE;
	getTexImage( level, format, GL_UNSIGNED_BYTE, aubOutput );
}

// virtual
void GLTexture::getUnsignedByte3Data( ubyte* aubOutput, int level )
{
	GLenum format = GL_RGB;
	getTexImage( level, format, GL_UNSIGNED_BYTE, aubOutput );
}

// virtual
void GLTexture::getUnsignedByte4Data( ubyte* aubOutput, int level )
{
	GLenum format = GL_RGBA;
	getTexImage( level, format, GL_UNSIGNED_BYTE, aubOutput );
}

//////////////////////////////////////////////////////////////////////////
// Protected
//////////////////////////////////////////////////////////////////////////

GLTexture::GLTexture( GLenum eTarget, GLTexture::GLTextureInternalFormat internalFormat ) :

	m_eTarget( eTarget ),
	m_eInternalFormat( internalFormat )

{
	glGenTextures( 1, &m_iTextureId );
	glBindTexture( m_eTarget, m_iTextureId );

	switch( internalFormat )
	{
	case GLTexture::DEPTH_COMPONENT_FLOAT_16:
		m_nComponents = 1;
		m_nBitsPerComponent = 16;
		break;

	case GLTexture::DEPTH_COMPONENT_FLOAT_24:
		m_nComponents = 1;
		m_nBitsPerComponent = 24;
		break;

	case GLTexture::DEPTH_COMPONENT_FLOAT_32:
		m_nComponents = 1;
		m_nBitsPerComponent = 32;
		break;

	case GLTexture::LUMINANCE_UNSIGNED_BYTE_8:
		m_nComponents = 1;
		m_nBitsPerComponent = 8;
		break;

	case GLTexture::LUMINANCE_FLOAT_16:
		m_nComponents = 1;
		m_nBitsPerComponent = 16;
		break;

	case GLTexture::LUMINANCE_FLOAT_32:
		m_nComponents = 1;
		m_nBitsPerComponent = 32;
		break;

	case GLTexture::RGBA_UNSIGNED_BYTE_8:
		m_nComponents = 1;
		m_nBitsPerComponent = 8;
		break;

	case GLTexture::RGB_FLOAT_16:
		m_nComponents = 3;
		m_nBitsPerComponent = 16;
		break;

	case GLTexture::RGB_FLOAT_32:
		m_nComponents = 3;
		m_nBitsPerComponent = 32;
		break;

	case GLTexture::RGBA_FLOAT_16:
		m_nComponents = 4;
		m_nBitsPerComponent = 16;
		break;

	case GLTexture::RGBA_FLOAT_32:
		m_nComponents = 4;
		m_nBitsPerComponent = 32;
		break;
	}
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

void GLTexture::getTexImage( GLint level, GLenum format, GLenum type, void* avOutput )
{
	bind();
	glGetTexImage( m_eTarget, level, format, type, avOutput );
}
