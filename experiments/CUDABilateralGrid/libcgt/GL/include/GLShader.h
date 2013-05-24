#ifndef GL_SHADER_H
#define GL_SHADER_H

#include <GL/glew.h>

class GLShader
{
public:

	// Factory constructors
	static GLShader* vertexShaderFromFile( const char* filename );
	static GLShader* fragmentShaderFromFile( const char* filename );

	// Destructor
	virtual ~GLShader();

	bool compile();
	bool isCompiled();

	GLuint getHandle();

private:

	GLuint m_iShaderHandle;
	GLchar* m_szCode;

	bool m_bIsCompiled;

	GLShader();
	static GLShader* fromFile( const char* filename, GLenum shaderType );	
};

#endif
