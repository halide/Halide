#if 0
// TODO: refactor into its own project

#include "Cg/CgShared.h"

#include <cstdio> // for stderr
#include <cstdlib> // just for NULL

//////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////

// static
CgShared* CgShared::getInstance()
{
	if( s_singleton == NULL )
	{
		s_singleton = new CgShared;
	}

	return s_singleton;
}

void CgShared::cgSharedErrorHandler( CGcontext ctx, CGerror err, void* appdata )
{
	fprintf( stderr, "Cg error: %s\n", cgGetErrorString( err ) );
	const char* listing = cgGetLastListing( ctx );
	if( listing != NULL )
	{
		fprintf( stderr, "last listing: %s\n", listing );
	}
}

CGcontext CgShared::getSharedCgContext() const
{
	return m_cgContext;
}

CGprofile CgShared::getLatestVertexProfile() const
{
	return m_cgVertexProfile;
}

CGprofile CgShared::getLatestFragmentProfile() const
{
	return m_cgFragmentProfile;
}

//////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////

CgShared::CgShared()
{
	m_cgContext = cgCreateContext();
	cgSetAutoCompile( m_cgContext, CG_COMPILE_IMMEDIATE );
	cgGLRegisterStates( m_cgContext );
	cgGLSetManageTextureParameters( m_cgContext, CG_TRUE );

	cgSetErrorHandler( CgShared::cgSharedErrorHandler, NULL );

	m_cgVertexProfile = cgGLGetLatestProfile( CG_GL_VERTEX );
	cgGLSetOptimalOptions( m_cgVertexProfile );

	m_cgFragmentProfile = cgGLGetLatestProfile( CG_GL_FRAGMENT );
	cgGLSetOptimalOptions( m_cgFragmentProfile );

#if _DEBUG

	printf( "Latest vertex profile is: %s\n", cgGetProfileString( m_cgVertexProfile ) );
	printf( "Latest fragment profile is: %s\n", cgGetProfileString( m_cgFragmentProfile ) );

#endif
}

// virtual
CgShared::~CgShared()
{
#if _DEBUG
	printf( "destroying cg shared\n" );
#endif
	cgDestroyContext( m_cgContext );
}

CgShared* CgShared::s_singleton = NULL;

#endif