#ifndef VERTEX_POSITION4F_TEXTURE2F_H
#define VERTEX_POSITION4F_TEXTURE2F_H

#include <D3D11.h>
#include <vecmath/Vector2f.h>
#include <vecmath/Vector4f.h>

struct VertexPosition4fTexture2f
{
	VertexPosition4fTexture2f();
	VertexPosition4fTexture2f( float x, float y, float z, float w, float u, float v );
	VertexPosition4fTexture2f( const Vector4f& position, const Vector2f& texture );

	Vector4f position;
	Vector2f texture;

	static int numElements();
	static int sizeInBytes();
	static D3D11_INPUT_ELEMENT_DESC s_layout[];
};

#endif // VERTEX_POSITION4F_TEXTURE2F_H
