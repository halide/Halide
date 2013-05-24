#if 0
// TODO: refactor into its own project

#include "Cg/CgProgramManager.h"

#include <cassert>

#include "Cg/CgProgramWrapper.h"
#include "Cg/CgShared.h"

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
CgProgramManager* CgProgramManager::getInstance()
{
	if( s_singleton == NULL )
	{
		s_singleton = new CgProgramManager;
	}
	return s_singleton;
}

// virtual
CgProgramManager::~CgProgramManager()
{
	// do nothing, programs are reference counted
}

bool CgProgramManager::loadVertexProgramFromFile( QString programName,
												 QString filename, QString entryPoint,
												 const char** args )
{
	return loadProgramFromFile( programName,
		filename, entryPoint,
		CgShared::getInstance()->getLatestVertexProfile(),
		args );
}

bool CgProgramManager::loadFragmentProgramFromFile( QString programName,
												   QString filename, QString entryPoint,
												   const char** args )
{
	return loadProgramFromFile( programName,
		filename, entryPoint,
		CgShared::getInstance()->getLatestFragmentProfile(),
		args );
}

bool CgProgramManager::loadProgramFromFile( QString programName,
										  QString filename, QString entryPoint,
										  CGprofile profile,
										  const char** args )
{
	CGcontext context;
	context = CgShared::getInstance()->getSharedCgContext();

	Reference< CgProgramWrapper > program = CgProgramWrapper::create( filename, entryPoint, context, profile );	
	bool succeeded = program.notNull();
	
	assert( succeeded );
	if( succeeded )
	{
		m_qhPrograms[ programName ] = program;
	}

	return succeeded;
}

Reference< CgProgramWrapper > CgProgramManager::getNamedProgram( QString programName )
{
	if( m_qhPrograms.contains( programName ) )
	{
		return m_qhPrograms.value( programName ) ;
	}
	else
	{
		assert( false );
		return NULL;
	}
}

void CgProgramManager::destroyNamedProgram( QString programName )
{
	m_qhPrograms.remove( programName );
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

CgProgramManager::CgProgramManager()
{

}

// static
CgProgramManager* CgProgramManager::s_singleton = NULL;

#endif