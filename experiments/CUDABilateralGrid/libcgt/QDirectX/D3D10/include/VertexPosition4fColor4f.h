#ifndef VERTEX_POSITION4F_COLOR4F_H
#define VERTEX_POSITION4F_COLOR4F_H

#include <d3d10_1.h>
#include <d3d10.h>
#include <vecmath/Vector3f.h>
#include <vecmath/Vector4f.h>

struct VertexPosition4fColor4f
{
	VertexPosition4fColor4f();
	VertexPosition4fColor4f( float x, float y, float z, float r, float g, float b );
	VertexPosition4fColor4f( float x, float y, float z, float w, float r, float g, float b, float a );
	VertexPosition4fColor4f( Vector3f position, Vector4f color );
	VertexPosition4fColor4f( Vector4f position, Vector4f color );

	Vector4f m_position;
	Vector4f m_color;

	static int numElements();
	static int sizeInBytes();
	static D3D10_INPUT_ELEMENT_DESC s_layout[];
};

#endif // VERTEX_POSITION4F_COLOR4F_H
