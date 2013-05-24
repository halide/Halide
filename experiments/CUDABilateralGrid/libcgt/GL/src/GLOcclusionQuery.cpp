#include "GL/GLOcclusionQuery.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
GLuint GLOcclusionQuery::getCurrentQuery()
{
	GLint currentQueryId;
	glGetQueryiv( GL_SAMPLES_PASSED, GL_CURRENT_QUERY, &currentQueryId );
	return static_cast< GLuint >( currentQueryId );
}

// static
GLint GLOcclusionQuery::nBits()
{
	GLint nBits;
	glGetQueryiv( GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS, &nBits );
	return nBits;
}

GLOcclusionQuery::GLOcclusionQuery()
{
	glGenQueries( 1, &m_uiQueryId );
}

// virtual
GLOcclusionQuery::~GLOcclusionQuery()
{
	glDeleteQueries( 1, &m_uiQueryId );
}

GLuint GLOcclusionQuery::getQueryId()
{
	return m_uiQueryId;
}

void GLOcclusionQuery::begin()
{
	glBeginQuery( GL_SAMPLES_PASSED, m_uiQueryId );
}

void GLOcclusionQuery::end()
{
	glEndQuery( GL_SAMPLES_PASSED );
}

bool GLOcclusionQuery::isResultAvailable()
{
	GLuint resultAvailable;
	glGetQueryObjectuiv( m_uiQueryId, GL_QUERY_RESULT_AVAILABLE, &resultAvailable );
	return( resultAvailable == GL_TRUE );
}

GLuint GLOcclusionQuery::getResult()
{
	GLuint result;
	glGetQueryObjectuiv( m_uiQueryId, GL_QUERY_RESULT, &result );
	return result;
}
