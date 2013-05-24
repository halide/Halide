#ifndef GL_OCCLUSION_QUERY_H
#define GL_OCCLUSION_QUERY_H

#include <GL/glew.h>

class GLOcclusionQuery
{
public:	

	static GLuint getCurrentQuery();
	static GLint nBits();

	GLOcclusionQuery();
	virtual ~GLOcclusionQuery();
	GLuint getQueryId();

	void begin();
	void end();

	bool isResultAvailable();
	GLuint getResult();	

private:

	GLuint m_uiQueryId;

};

#endif
