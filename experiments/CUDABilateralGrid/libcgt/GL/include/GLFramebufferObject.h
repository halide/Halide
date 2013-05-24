#ifndef GL_FRAMEBUFFER_OBJECT_H
#define GL_FRAMEBUFFER_OBJECT_H

#include <GL/glew.h>
#include <cstdio>

#include <common/BasicTypes.h>

class GLTexture;

class GLFramebufferObject
{
public:

	static void disableAll();
	static GLint getMaxColorAttachments();

	GLFramebufferObject();
	virtual ~GLFramebufferObject();

	void bind();

	// TODO: GLTexture2D extends GLTexture
	// GLTexture->getType()
	// forget mipmapped textures for a while

	// eAttachment can be COLOR_ATTACHMENT0_EXT, ... COLOR_ATTACHMENTn_EXT,
	// DEPTH_ATTACHMENT_EXT, STENCIL_ATTACHMENT_EXT
	void attachTexture( GLenum eAttachment, GLTexture* pTexture, GLint zSlice = 0 );
	void detachTexture( GLenum eAttachment );

	// TODO: attachRenderBuffer()

	GLuint getAttachedId( GLenum eAttachment );
	GLuint getAttachedType( GLenum eAttachment );
	GLint getAttachedZSlice( GLenum eAttachment );

	bool checkStatus( GLenum* pStatus = NULL );

private:	

	void guardedBind();
	void guardedUnbind();
	GLuint generateFBOId();

	GLuint m_iFramebufferObjectHandle;
	GLint m_iPreviousFramebufferObjectHandle; // yes, GLint to deal with glGetInteger weirdness
};

#endif
