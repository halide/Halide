#ifndef VERTEX_POSITION4F_NORMAL3F_COLOR4F_H
#define VERTEX_POSITION4F_NORMAL3F_COLOR4F_H

#include <d3d10_1.h>
#include <d3d10.h>
#include <vecmath/Vector3f.h>
#include <vecmath/Vector4f.h>

struct VertexPosition4fNormal3fColor4f
{
	VertexPosition4fNormal3fColor4f();
	VertexPosition4fNormal3fColor4f( Vector4f position, Vector3f normal, Vector4f color );

	Vector4f m_position;
	Vector3f m_normal;
	Vector4f m_color;

	static int numElements();
	static int sizeInBytes();
	static D3D10_INPUT_ELEMENT_DESC s_layout[];
};

#endif // VERTEX_POSITION4F_NORMAL3F_COLOR4F_H
