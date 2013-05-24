#ifndef GL_BUFFER_OBJECT_MANAGER_H
#define GL_BUFFER_OBJECT_MANAGER_H

#include <GL/glew.h>
#include <QHash>
#include <QString>

#include "GLBufferObject.h"

class GLBufferObjectManager
{
public:

	GLBufferObjectManager();
	virtual ~GLBufferObjectManager();

	void addBufferObject( QString bufferObjectName, GLBufferObject* pBuffer );
	void removeBufferObject( QString bufferObjectName );

	GLBufferObject* getBufferObject( QString bufferObjectName );

	// TODO: can a buffer be [simultaneously] bound to multiple targets?
	void bindBufferObjectToTarget( QString bufferObjectName, GLBufferObject::GLBufferObjectTarget target );
	void unbindBufferObject( QString bufferObjectName );

private:

	QHash< QString, GLBufferObject* > m_qNamesToBufferObjects;
	QHash< QString, GLBufferObject::GLBufferObjectTarget > m_qNamesToTargets;

};

#endif
