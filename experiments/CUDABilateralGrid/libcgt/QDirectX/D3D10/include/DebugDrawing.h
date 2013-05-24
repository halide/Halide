#ifndef DEBUGDRAWING_H
#define DEBUGDRAWING_H
// --------------------------------------------------------------------------

#include <vecmath/Vector3f.h>
#include <vecmath/Vector4f.h>
#include <vecmath/Matrix4f.h>
#include <common/Reference.h>

#include "DynamicVertexBuffer.h"
#include "EffectManager.h"
// --------------------------------------------------------------------------

class DebugDrawing
{
public:
	~DebugDrawing();

	static void init( ID3D10Device*, Reference< EffectManager > );

	static DebugDrawing* instance();

	// clear debug primitives from list, call in the beginning of frame
	void reset();

	void addPoint( const Vector3f& v, const Vector3f& c );
	void addLine( const Vector3f& v0, const Vector3f& v1 );
	void addLine( const Vector3f& v0, const Vector3f& v1, const Vector3f& c0, const Vector3f& c1 );
	void addTriangle( const Vector3f& v0, const Vector3f& v1, const Vector3f& v2, const Vector3f& c0, const Vector3f& c1, const Vector3f& c2 );

	// an axis-aligned wireframe box 
	void addBox( const Vector3f& vmin, const Vector3f& vmax );

	// renders debug primitives but does not clear the lists
	void draw( const Matrix4f& mWorldToClip );

protected:
	DebugDrawing( ID3D10Device*, Reference< EffectManager > );
	static DebugDrawing*				s_pInstance;

	ID3D10Device*						m_pDevice;
	ID3D10Effect*						m_pEffect;
	ID3D10InputLayout*					m_pInputLayout;
	Reference< DynamicVertexBuffer >	m_vb;
};
// --------------------------------------------------------------------------

#endif // !DEBUGDRAWING_H