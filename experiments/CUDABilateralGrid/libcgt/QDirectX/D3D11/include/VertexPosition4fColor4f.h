#ifndef VERTEX_POSITION4F_COLOR4F_H
#define VERTEX_POSITION4F_COLOR4F_H

#include <D3D11.h>
#include <vecmath/Vector3f.h>
#include <vecmath/Vector4f.h>

struct VertexPosition4fColor4f
{
	VertexPosition4fColor4f();
	VertexPosition4fColor4f( float x, float y, float z, float r, float g, float b );
	VertexPosition4fColor4f( float x, float y, float z, float w, float r, float g, float b, float a );
	VertexPosition4fColor4f( const Vector3f& _position, const Vector4f& _color );
	VertexPosition4fColor4f( const Vector4f& _position, const Vector4f& _color );

	Vector4f position;
	Vector4f color;

	static int numElements();
	static int sizeInBytes();
	static D3D11_INPUT_ELEMENT_DESC s_layout[];
};

#endif // VERTEX_POSITION4F_COLOR4F_H
