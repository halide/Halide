#include "GL/GLShared.h"

#include "GL/GLBufferObject.h"
#include "GL/GLFramebufferObject.h"
#include "GL/GLTextureRectangle.h"

#include "GL/GLVertexBufferObject.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
Reference< GLShared > GLShared::getInstance()
{
	if( s_singleton.operator ==( NULL ) )
	{
		s_singleton = new GLShared;
	}

	return s_singleton;
}

// virtual
GLShared::~GLShared()
{
	QList< QVector< GLTextureRectangle* > > sharedTextureVectors = m_qhSharedTextures.values();
	foreach( QVector< GLTextureRectangle* > qvTextureVector, sharedTextureVectors )
	{
		foreach( GLTextureRectangle* pTexture, qvTextureVector )
		{
			delete pTexture;
		}
		qvTextureVector.clear();
	}
	m_qhSharedTextures.clear();
}

Reference< GLFramebufferObject > GLShared::getSharedFramebufferObject()
{
	return m_fbo;
}

QVector< GLTextureRectangle* > GLShared::getSharedTexture( int width, int height, int count )
{
	QPair< int, int > key = qMakePair( width, height );

	// stick one in the cache if it's empty
	if( !( m_qhSharedTextures.contains( key ) ) )
	{
		m_qhSharedTextures.insert( key, QVector< GLTextureRectangle* >() );
	}

	// now grab it back
	QVector< GLTextureRectangle* > textureVector = m_qhSharedTextures.value( key );
	int nAllocatedTextures = textureVector.size();

	// check if we have already allocated count textures
	// if not, allocate some more
	if( nAllocatedTextures < count )
	{
		for( int i = nAllocatedTextures; i < count; ++i )
		{
			textureVector.append( GLTextureRectangle::createFloat4Texture( width, height ) );
		}

		// now stick it back in the hash map
		// TODO: can probably do this by storing QHash of QVector*'s
		// so don't have to reinsert
		m_qhSharedTextures.insert( key, textureVector );
	}

	QVector< GLTextureRectangle* > returnTextureVector;
	for( int i = 0; i < count; ++i )
	{
		returnTextureVector.append( textureVector.at( i ) );
	}

	return returnTextureVector;
}

GLBufferObject* GLShared::getSharedXYCoordinateVBO( int width, int height )
{
	QPair< int, int > key = qMakePair( width, height );

	// stick it in the cache if it's empty
	if( !( m_qhSharedXYVBOs.contains( key ) ) )
	{
		GLshort* xyCoords = new GLshort[ 2 * width * height ];
		int index = 0;

		for( int y = 0; y < height; ++y )
		{
			for( int x = 0; x < width; ++x )
			{
				xyCoords[ index ] = x;
				xyCoords[ index + 1 ] = y;

				index += 2;
			}
		}

		GLBufferObject* pVBO = GLVertexBufferObject::fromShortArray
		(
			xyCoords,
			2 * width * height,
			width * height
		);

		delete[] xyCoords;

		m_qhSharedXYVBOs.insert( key, pVBO );
	}

	return m_qhSharedXYVBOs.value( key );
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

GLShared::GLShared()
{
	m_fbo = new GLFramebufferObject;
}

Reference< GLShared > GLShared::s_singleton = NULL;
