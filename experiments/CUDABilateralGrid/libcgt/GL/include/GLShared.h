#ifndef GL_SHARED_H
#define GL_SHARED_H

// singleton class for sharing simple OpenGL objects

#include <GL/glew.h>
#include <QPair>
#include <QHash>
#include <QVector>

#include "common/Reference.h"

class GLBufferObject;
class GLFramebufferObject;
class GLTextureRectangle;

class GLShared
{
public:

	static Reference< GLShared > getInstance();
	virtual ~GLShared();

	Reference< GLFramebufferObject > getSharedFramebufferObject();

	// request count textures of size width by height
	QVector< GLTextureRectangle* > getSharedTexture( int width, int height, int count );

	// request a buffer object of size width by height
	GLBufferObject* getSharedXYCoordinateVBO( int width, int height );

private:

	GLShared();	

	static Reference< GLShared > s_singleton;
	
	Reference< GLFramebufferObject > m_fbo;

	QHash< QPair< int, int >, QVector< GLTextureRectangle* > > m_qhSharedTextures;
	QHash< QPair< int, int >, GLBufferObject* > m_qhSharedXYVBOs;
};

#endif
