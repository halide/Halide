#ifndef VERTEX_POSITION4F_H
#define VERTEX_POSITION4F_H

#include <d3d10_1.h>
#include <d3d10.h>
#include <vecmath/Vector4f.h>

struct VertexPosition4f
{
	VertexPosition4f();
	VertexPosition4f( float x, float y, float z, float w );
	VertexPosition4f( Vector4f position );

	Vector4f m_position;

	static int numElements();
	static int sizeInBytes();
	static D3D10_INPUT_ELEMENT_DESC s_layout[];
};

#endif // VERTEX_POSITION4F_H
