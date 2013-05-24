#if 0

#ifndef CG_SHARED_H
#define CG_SHARED_H

// singleton class for simple Cg projects
// that share a single CGContext

#include <GL/glew.h>
#include <Cg/cg.h>
#include <Cg/cgGL.h>

#include "common/Reference.h"

class CgShared
{
public:

	static CgShared* getInstance();
	virtual ~CgShared();

	static void cgSharedErrorHandler( CGcontext ctx, CGerror err, void* appdata );

	CGcontext getSharedCgContext() const;
	CGprofile getLatestVertexProfile() const;
	CGprofile getLatestFragmentProfile() const;

private:

	CgShared();	

	static CgShared* s_singleton;

	CGcontext m_cgContext;
	CGprofile m_cgVertexProfile;
	CGprofile m_cgFragmentProfile;
};

#endif

#endif