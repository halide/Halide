#include "GL/GLFramebufferObject.h"

#include "GL/GLTexture2D.h"

#include <cassert>
#include <cstdio>

// ========================================
// Public
// ========================================

// static
void GLFramebufferObject::disableAll()
{
	glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, 0 );
}

// static
GLint GLFramebufferObject::getMaxColorAttachments()
{
	GLint maxColorAttachments;
	glGetIntegerv( GL_MAX_COLOR_ATTACHMENTS_EXT, &maxColorAttachments );
	return maxColorAttachments;
}

GLFramebufferObject::GLFramebufferObject() :
	m_iFramebufferObjectHandle( generateFBOId() ),
	m_iPreviousFramebufferObjectHandle( 0 )
{	
	guardedBind();
	guardedUnbind();
}

// virtual
GLFramebufferObject::~GLFramebufferObject()
{
	glDeleteFramebuffersEXT( 1, &m_iFramebufferObjectHandle );
}

void GLFramebufferObject::bind()
{
	glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, m_iFramebufferObjectHandle );
}

void GLFramebufferObject::attachTexture( GLenum eAttachment, GLTexture* pTexture, GLint zSlice )
{
	guardedBind();

	// TODO: 1d texture?
	// rectangle can be target, not the same as type apparently
	// also cube maps

	GLuint textureId = pTexture->getTextureId();

	if( getAttachedId( eAttachment ) != textureId )
	{
		GLenum textureTarget = pTexture->getTarget();
		switch( textureTarget )
		{
		case GL_TEXTURE_2D:
			
			glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, eAttachment,
				GL_TEXTURE_2D, textureId,
				0 ); // mipmap level
			break;

		case GL_TEXTURE_RECTANGLE_ARB:
			glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, eAttachment,
				GL_TEXTURE_RECTANGLE_ARB, textureId,
				0 ); // mipmap level
			break;

		case GL_TEXTURE_3D:

			glFramebufferTexture3DEXT( GL_FRAMEBUFFER_EXT, eAttachment,
				GL_TEXTURE_3D, textureId,
				0, // mipmap level
				zSlice );
			break;

		default:

			assert( false );
			break;
		}
	}	

	guardedUnbind();
}

void GLFramebufferObject::detachTexture( GLenum eAttachment )
{
	guardedBind();

	GLenum type = getAttachedType( eAttachment );
	switch( type )
	{
	case GL_NONE:
		break;
	case GL_RENDERBUFFER_EXT:
		glFramebufferRenderbufferEXT( GL_FRAMEBUFFER_EXT, eAttachment, GL_RENDERBUFFER_EXT, 0 ); // 0 ==> detach
		break;
	case GL_TEXTURE:
		glFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, eAttachment,
			GL_TEXTURE_2D, // ignored, since detaching
			0, // 0 ==> detach
			0 ); // mipmap level
		break;
	default:
		fprintf( stderr, "FramebufferObject::unbind_attachment ERROR: Unknown attached resource type\n" );
		assert( false );
		break;
	}

	guardedUnbind();
}

GLuint GLFramebufferObject::getAttachedId( GLenum eAttachment )
{
	guardedBind();

	GLint id;
	glGetFramebufferAttachmentParameterivEXT( GL_FRAMEBUFFER_EXT, eAttachment,
		GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT, &id );

	guardedUnbind();

	return id;
}

GLuint GLFramebufferObject::getAttachedType( GLenum eAttachment )
{
	guardedBind();

	GLint type = 0;
	glGetFramebufferAttachmentParameterivEXT( GL_FRAMEBUFFER_EXT, eAttachment,
		GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT,
		&type );

	guardedUnbind();

	return type;
}

GLint GLFramebufferObject::getAttachedZSlice( GLenum eAttachment )
{
	guardedBind();

	GLint slice = -1;
	glGetFramebufferAttachmentParameterivEXT( GL_FRAMEBUFFER_EXT, eAttachment,
		GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_3D_ZOFFSET_EXT,
		&slice );
	
	guardedUnbind();

	return slice;
}

bool GLFramebufferObject::checkStatus( GLenum* pStatus )
{
	guardedBind();

	bool isComplete = false;

	GLenum status = glCheckFramebufferStatusEXT( GL_FRAMEBUFFER_EXT );
	switch( status )
	{
	case GL_FRAMEBUFFER_COMPLETE_EXT:
		// fprintf( stderr, "Framebuffer is complete.\n" );
		isComplete = true;
		break;
	case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
		fprintf( stderr, "Framebuffer incomplete: format unsupported.\n" );
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
		fprintf( stderr, "Framebuffer incomplete: incomplete attachment.\n" );
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
		fprintf( stderr, "Framebuffer incomplete: missing attachment.\n" );
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
		fprintf( stderr, "Framebuffer incomplete: dimension mismatch.\n" );
		break;		
	// TODO: wtf: this enum is missing?
	//case GL_FRAMEBUFFER_INCOMPLETE_DUPLICATE_ATTACHMENT_EXT:
	//	fprintf( stderr, "framebuffer INCOMPLETE_DUPLICATE_ATTACHMENT\n" );
	//	break;
	case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
		fprintf( stderr, "Framebuffer incomplete: incompatible formats.\n" );
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:
		fprintf( stderr, "framebuffer INCOMPLETE_DRAW_BUFFER\n" );
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:
		fprintf( stderr, "framebuffer INCOMPLETE_READ_BUFFER\n" );
		break;
	case GL_FRAMEBUFFER_BINDING_EXT:
		fprintf( stderr, "framebuffer BINDING_EXT\n" );
		break;
	default:
		fprintf( stderr, "Can't get here!\n" );
		assert( false );
	}

	guardedUnbind();

	if( pStatus != NULL )
	{
		*pStatus = status;
	}
	return isComplete;
}

void GLFramebufferObject::guardedBind()
{
	// Only binds if handle is different than the currently bound FBO
	glGetIntegerv( GL_FRAMEBUFFER_BINDING_EXT, &m_iPreviousFramebufferObjectHandle );
	if( m_iFramebufferObjectHandle != ( ( GLuint )m_iPreviousFramebufferObjectHandle ) )
	{
		glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, m_iFramebufferObjectHandle );
	}
}

void GLFramebufferObject::guardedUnbind()
{
	// Returns FBO binding to the previously enabled FBO
	if( ( ( GLuint )m_iPreviousFramebufferObjectHandle ) != m_iFramebufferObjectHandle )
	{
		glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, ( GLuint )m_iPreviousFramebufferObjectHandle );
	}
}

GLuint GLFramebufferObject::generateFBOId()
{
	GLuint id = 0;
	glGenFramebuffersEXT( 1, &id );
	return id;
}

#if 0

glReadBuffer(GL_COLOR_ATTACHMENT0_EXT); // FBO version
glReadPixels(0,0,texSize,texSize,texture_format,GL_FLOAT,data);

#endif