#if 0
// TODO: refactor into its own project

#include "Cg/CgProgramWrapper.h"

#include <cassert>

// static
Reference< CgProgramWrapper > CgProgramWrapper::create( QString filename, QString entryPoint, CGcontext context, CGprofile profile )
{
	CGprogram program = cgCreateProgramFromFile
	(
		context,
		CG_SOURCE,
		qPrintable( filename ),
		profile,
		qPrintable( entryPoint ),
		NULL
	);

	if( program != NULL )
	{
		return new CgProgramWrapper( program );
	}
	else
	{
#if _DEBUG
		fprintf( stderr, "%s\n", cgGetLastListing( context ) );
#endif
		return NULL;
	}
}

// virtual
CgProgramWrapper::~CgProgramWrapper()
{
	printf( "deleting cg program 0x%p\n", ( void* )this );
	cgDestroyProgram( m_cgProgram );
}

Reference< CgParameterSet > CgProgramWrapper::createParameterSet()
{
	return new CgParameterSet( m_programParameters );
}

bool CgProgramWrapper::isCompiled()
{
	return cgIsProgramCompiled( m_cgProgram );
}

void CgProgramWrapper::bind()
{
	assert( isLoaded() );
	cgGLBindProgram( m_cgProgram );
}

void CgProgramWrapper::load()
{
	cgGLLoadProgram( m_cgProgram );
}

bool CgProgramWrapper::isLoaded()
{
	return cgGLIsProgramLoaded( m_cgProgram );
}

CGprogram CgProgramWrapper::getProgram()
{
	return m_cgProgram;
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

CgProgramWrapper::CgProgramWrapper( CGprogram program ) :

	m_cgProgram( program )

{
	load();

	CGparameter param = cgGetFirstParameter( m_cgProgram, CG_PROGRAM );
	while( param != NULL )
	{
		if( cgGetParameterVariability( param ) == CG_UNIFORM )
		{
			QString name = cgGetParameterName( param );
			m_programParameters.insert( name, param );
		}

		param = cgGetNextParameter( param );
	}
}

#endif