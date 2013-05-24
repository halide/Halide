#ifndef CG_PROGRAM_WRAPPER_H
#define CG_PROGRAM_WRAPPER_H

#include <GL/glew.h>
#include <Cg/cg.h>
#include <Cg/cgGL.h>

#include <QHash>
#include <QString>

#include "common/Reference.h"
#include "Cg/CgParameterSet.h"

class CgProgramWrapper
{
public:
	
	static Reference< CgProgramWrapper > create( QString filename, QString entryPoint, CGcontext context, CGprofile profile );
	virtual ~CgProgramWrapper();

	Reference< CgParameterSet > createParameterSet();
	
	bool isCompiled();

	// applies all the parameters in the set
	// and then binds the program for execution
	void bind();

	// loads the program into the GL runtime
	void load();
	bool isLoaded();

	// returns the stored program
	CGprogram getProgram();

private:

	CgProgramWrapper( CGprogram program );

	CGprogram m_cgProgram;
	QHash< QString, CGparameter > m_programParameters;

};

#endif
