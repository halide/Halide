#if 0

#ifndef CG_UTILITIES_H
#define CG_UTILITIES_H

#include <cstdio>
#include <GL/glew.h>
#include <Cg/cg.h>
#include <Cg/cgGL.h>

class CgUtilities
{
public:

	// enables / disables the latest vertex and fragment profiles
	static void setShadersEnabled( bool b );

	// enables / disables the latest vertex profile
	static void setVertexShadersEnabled( bool b );

	// enables / disables the latest fragment profile
	static void setFragmentShadersEnabled( bool b );

	static void setupOrthoCamera( int viewportWidth, int viewportHeight,
		CGparameter modelViewProjectionParameter );

	static bool validateTechnique( CGtechnique technique, CGcontext context = NULL );
};

#endif // CG_UTILITIES_H

#endif