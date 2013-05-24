#ifndef CG_PROGRAM_MANAGER_H
#define CG_PROGRAM_MANAGER_H

#include <GL/glew.h>
#include <Cg/cg.h>
#include <Cg/cgGL.h>
#include <QHash>
#include <QString>

#include "common/Reference.h"

class CgProgramWrapper;

class CgProgramManager
{
public:

	static CgProgramManager* getInstance();
	virtual ~CgProgramManager();

	// Convenience method for loading vertex programs with the latest vertex profile
	// calls loadProgramFromFile()
	bool loadVertexProgramFromFile( QString programName,
		QString filename, QString entryPoint,
		const char** args = NULL );

	// Convenience method for loading fragment programs with the latest vertex profile
	// calls loadProgramFromFile()
	bool loadFragmentProgramFromFile( QString programName,
		QString filename, QString entryPoint,
		const char** args = NULL );

	// Loads the program contained in filename with entryPoint into the shared cg context
	// The program is added to an internal table under programName
	bool loadProgramFromFile( QString programName,
		QString filename, QString entryPoint,
		CGprofile profile,
		const char** args = NULL );

	Reference< CgProgramWrapper > getNamedProgram( QString programName );
	void destroyNamedProgram( QString programName );

private:

	// =========================================================================
	// Methods
	// =========================================================================

	CgProgramManager();	

	// =========================================================================
	// Fields
	// =========================================================================

	static CgProgramManager* s_singleton;

	QHash< QString, Reference< CgProgramWrapper > > m_qhPrograms;
};

#endif
