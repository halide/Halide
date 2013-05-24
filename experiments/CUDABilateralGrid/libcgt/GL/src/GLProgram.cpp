#include "GL/GLProgram.h"

#include "GL/GLShader.h"
#include "GL/GLTexture.h"
#include "vecmath/libcgt_vecmath.h"

GLProgram::GLProgram() :
	m_bIsLinked( false )
{
	m_iProgramHandle = glCreateProgram();
}

GLProgram::~GLProgram()
{
	glDeleteProgram( m_iProgramHandle );
}

GLhandleARB GLProgram::getHandle()
{
	return m_iProgramHandle;
}

void GLProgram::attachShader( GLShader* pShader )
{
	glAttachShader( m_iProgramHandle, pShader->getHandle() );
}

void GLProgram::detachShader( GLShader* pShader )
{
	glDetachShader( m_iProgramHandle, pShader->getHandle() );
}

GLint GLProgram::getNumActiveUniforms()
{
	GLint numActiveUniforms;
	glGetProgramiv( m_iProgramHandle, GL_ACTIVE_UNIFORMS, &numActiveUniforms );
	return numActiveUniforms;
}

GLint GLProgram::getUniformLocation( const GLchar* szName )
{
	return glGetUniformLocation( m_iProgramHandle, szName );
}

void GLProgram::getUniformMatrix4f( GLint iUniformLocation, Matrix4f* pMatrix )
{
	glGetUniformfv( m_iProgramHandle, iUniformLocation, *pMatrix );
}

void GLProgram::setUniformMatrix4f( GLint iUniformLocation, Matrix4f* pMatrix )
{
	glUniformMatrix4fv( iUniformLocation, 1, false, *pMatrix );
}

void GLProgram::setUniformSampler( GLint iUniformLocation, GLTexture* pTexture )
{
	glUniform1i( iUniformLocation, 0 /*pTexture->getTextureUnit()*/ );
}

void GLProgram::setUniformSampler( const GLchar* szName, GLTexture* pTexture )
{
	// TODO: do error checking
	setUniformSampler( getUniformLocation( szName ), pTexture );
}

void GLProgram::setUniformVector2f( const GLchar* szName, float x, float y )
{
	int uniformLocation = getUniformLocation( szName );
	glUniform2f( uniformLocation, x, y );
}

void GLProgram::setUniformVector2f( const GLchar* szName, Vector2f* pv )
{
	int uniformLocation = getUniformLocation( szName );
    glUniform2f( uniformLocation, pv->x(), pv->y() );
}

void GLProgram::setUniformVector3f( const GLcharARB* szName, float x, float y, float z )
{
	glUniform3f( getUniformLocation( szName ), x, y, z );
}

void GLProgram::setUniformVector3f( const GLcharARB* szName, Vector3f* pv )
{
    glUniform3f( getUniformLocation( szName ), pv->x(), pv->y(), pv->z() );
}

bool GLProgram::link()
{
	GLint status;

	glLinkProgram( m_iProgramHandle );
	glGetProgramiv( m_iProgramHandle, GL_OBJECT_LINK_STATUS_ARB, &status );

	if( status == GL_TRUE )
	{
		m_bIsLinked = true;
	}
	return m_bIsLinked;
}

bool GLProgram::isLinked()
{
	return m_bIsLinked;
}

bool GLProgram::use()
{
	if( m_bIsLinked )
	{
		glUseProgram( m_iProgramHandle );
	}
	return m_bIsLinked;
}

// static
void GLProgram::disableAll()
{
	glUseProgram( 0 );
}
