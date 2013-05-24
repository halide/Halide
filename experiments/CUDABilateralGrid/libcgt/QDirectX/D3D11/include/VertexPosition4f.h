#ifndef VERTEX_POSITION4F_H
#define VERTEX_POSITION4F_H

#include <D3D11.h>
#include <vecmath/Vector4f.h>

struct VertexPosition4f
{
	VertexPosition4f();
	VertexPosition4f( float x, float y, float z, float w );
	VertexPosition4f( const Vector4f& _position );

	Vector4f position;

	static int numElements();
	static int sizeInBytes();
	static D3D11_INPUT_ELEMENT_DESC s_layout[];
};

#endif // VERTEX_POSITION4F_H
