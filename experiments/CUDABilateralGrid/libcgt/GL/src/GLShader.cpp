#include <cstdio>

#include "GL/GLShader.h"

#include "io/FileReader.h"

GLShader* GLShader::vertexShaderFromFile( const char* filename )
{
	return GLShader::fromFile( filename, GL_VERTEX_SHADER );
}

GLShader* GLShader::fragmentShaderFromFile( const char* filename )
{
	return GLShader::fromFile( filename, GL_FRAGMENT_SHADER );
}

GLuint GLShader::getHandle()
{
	return m_iShaderHandle;
}

GLShader::~GLShader()
{
	glDeleteShader( m_iShaderHandle );
	m_iShaderHandle = -1;

	delete[] m_szCode;
	m_szCode = NULL;
}

bool GLShader::compile()
{
	GLint status;

	glCompileShader( m_iShaderHandle );

    //get the compilation log
    const int MaxLogLength = 1000;
    char shaderLog[MaxLogLength + 1];
    int LogLength;
    glGetShaderInfoLog( m_iShaderHandle, MaxLogLength, &LogLength, shaderLog );

    printf( "%s", shaderLog );

	glGetShaderiv( m_iShaderHandle, GL_OBJECT_COMPILE_STATUS_ARB, &status );

	if( status == GL_TRUE )
	{
		m_bIsCompiled = true;
	}
	return m_bIsCompiled;
}

bool GLShader::isCompiled()
{
	return m_bIsCompiled;
}

// ==================== Private ====================

GLShader::GLShader() :
	m_iShaderHandle( 0 ),
	m_szCode( NULL ),
	m_bIsCompiled( false )
{

}

// static
GLShader* GLShader::fromFile( const char* filename, GLenum eShaderType )
{
	char* buffer;
	long length;

	if( FileReader::readTextFile( filename, &buffer, &length ) )
	{
		GLShader* pShader = new GLShader;
		pShader->m_szCode = buffer;
		pShader->m_iShaderHandle = glCreateShader( eShaderType );
		glShaderSource( pShader->m_iShaderHandle, 1,
			( const GLchar ** )( &( pShader->m_szCode ) ), NULL ); // TODO: figure out the const correctness

		return pShader;
	}
	else
	{
		return NULL;
	}
}
