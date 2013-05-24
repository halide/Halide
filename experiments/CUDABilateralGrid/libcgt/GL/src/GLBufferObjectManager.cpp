#include "GL/GLBufferObjectManager.h"

#include <cassert>

GLBufferObjectManager::GLBufferObjectManager()
{

}

// virtual
GLBufferObjectManager::~GLBufferObjectManager()
{
	foreach( GLBufferObject* pBuffer, m_qNamesToBufferObjects.values() )
	{
		delete pBuffer;
	}

	foreach( GLBufferObject::GLBufferObjectTarget target, m_qNamesToTargets.values() )
	{
		glBindBuffer( target, 0 );
	}
}

void GLBufferObjectManager::addBufferObject( QString bufferObjectName, GLBufferObject* pBuffer )
{
	if( m_qNamesToBufferObjects.contains( bufferObjectName ) )
	{
		delete m_qNamesToBufferObjects[ bufferObjectName ];
		m_qNamesToBufferObjects.remove( bufferObjectName );
	}

	m_qNamesToBufferObjects[ bufferObjectName ] = pBuffer;
}

void GLBufferObjectManager::removeBufferObject( QString bufferObjectName )
{
	if( m_qNamesToBufferObjects.contains( bufferObjectName ) )
	{
		delete m_qNamesToBufferObjects[ bufferObjectName ];
		m_qNamesToBufferObjects.remove( bufferObjectName );
	}
}

GLBufferObject* GLBufferObjectManager::getBufferObject( QString bufferObjectName )
{
	if( m_qNamesToBufferObjects.contains( bufferObjectName ) )
	{
		return m_qNamesToBufferObjects[ bufferObjectName ];
	}
	else
	{
		return NULL;
	}
}

void GLBufferObjectManager::bindBufferObjectToTarget( QString bufferObjectName, GLBufferObject::GLBufferObjectTarget target )
{
	if( m_qNamesToBufferObjects.contains( bufferObjectName ) )
	{
		GLBufferObject* pBufferObject = m_qNamesToBufferObjects[ bufferObjectName ];
		pBufferObject->bind( target );
		m_qNamesToTargets[ bufferObjectName ] = target;
	}
}

void GLBufferObjectManager::unbindBufferObject( QString bufferObjectName )
{
	// TODO: fix me
	assert( false );
#if 0
	if( m_qNamesToTargets.contains( bufferObjectName ) )
	{
		GLBufferObject* pBufferObject = m_qNamesToBufferObjects[ bufferObjectName ];
		pBufferObject->unbind();
		m_qNamesToTargets.remove( bufferObjectName );
	}
#endif
}
