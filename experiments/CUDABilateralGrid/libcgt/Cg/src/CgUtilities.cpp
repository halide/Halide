#if 0
// TODO: refactor into its own project

#include "Cg/CgUtilities.h"

#include <cassert>
#include <cstdio>

#include "Cg/CgShared.h"
#include "GL/GLUtilities.h"

// static
void CgUtilities::setShadersEnabled( bool b )
{
	setVertexShadersEnabled( b );
	setFragmentShadersEnabled( b );
}

// static
void CgUtilities::setVertexShadersEnabled( bool b )
{
	CGprofile vertexProfile = CgShared::getInstance()->getLatestVertexProfile();

	if( b )
	{
		cgGLEnableProfile( vertexProfile );
	}
	else
	{
		cgGLDisableProfile( vertexProfile );
	}
}

// static
void CgUtilities::setFragmentShadersEnabled( bool b )
{
	CGprofile fragmentProfile = CgShared::getInstance()->getLatestFragmentProfile();

	if( b )
	{
		cgGLEnableProfile( fragmentProfile );
	}
	else
	{
		cgGLDisableProfile( fragmentProfile );
	}
}

// static
void CgUtilities::setupOrthoCamera( int viewportWidth, int viewportHeight,
								   CGparameter modelViewProjectionParameter )
{
	GLUtilities::setupOrthoCamera( viewportWidth, viewportHeight );

	cgGLSetStateMatrixParameter
	(
		modelViewProjectionParameter,
		CG_GL_MODELVIEW_PROJECTION_MATRIX,
		CG_GL_MATRIX_IDENTITY
	);
}

// static
bool CgUtilities::validateTechnique( CGtechnique technique, CGcontext context )
{
	// default parameter for context is NULL
	// which means get the shared one
	if( context == NULL )
	{
		context = CgShared::getInstance()->getSharedCgContext();
	}

#if _DEBUG
	fprintf( stderr, "validating technique: %s\n", cgGetTechniqueName( technique ) );
#endif

	CGbool validated = cgValidateTechnique( technique );

#if _DEBUG
	if( validated == CG_FALSE )
	{
		CGerror error;
		fprintf( stderr, "last error = %s\n", cgGetLastErrorString( &error ) );
		fprintf( stderr, "last listing = %s\n", cgGetLastListing( context ) );
	}
	assert( validated == CG_TRUE );
#endif

	return( validated == CG_TRUE );
}

#endif