#ifndef GL_PROGRAM_H
#define GL_PROGRAM_H

#include <GL/glew.h>

class GLShader;
class Matrix4f;
class GLTexture;
class Vector2f;
class Vector3f;

class GLProgram
{
public:

	GLProgram();
	virtual ~GLProgram();

	GLuint getHandle();

	void attachShader( GLShader* pShader );
	void detachShader( GLShader* pShader );
	
	GLint getNumActiveUniforms();
	GLint getUniformLocation( const GLchar* szName );
	void getUniformMatrix4f( GLint iUniformLocation, Matrix4f* pMatrix );
		
	void setUniformMatrix4f( GLint iUniformLocation, Matrix4f* pMatrix );
	void setUniformSampler( GLint iUniformLocation, GLTexture* pTexture );
	void setUniformSampler( const GLchar* szName, GLTexture* pTexture );
	void setUniformVector2f( const GLchar* szName, float x, float y );
	void setUniformVector2f( const GLchar* szName, Vector2f* pv );
	void setUniformVector3f( const GLchar* szName, float x, float y, float z );
	void setUniformVector3f( const GLchar* szName, Vector3f* pv );

	bool link();
	bool isLinked();
	
	bool use();
	static void disableAll();

private:

	GLuint m_iProgramHandle;
	bool m_bIsLinked;
};

#endif
