#pragma once

#include <D3D11.h>
#include <vecmath/Vector2f.h>
#include <vecmath/Vector3f.h>
#include <vecmath/Vector4f.h>

struct VertexPosition4fNormal3fColor4fTexture2f
{
	VertexPosition4fNormal3fColor4fTexture2f();
	VertexPosition4fNormal3fColor4fTexture2f( const Vector4f& _position, const Vector3f& _normal, const Vector4f& _color, const Vector2f& _texture );

	Vector4f position;
	Vector3f normal;
	Vector4f color;
	Vector2f texture;

	static int numElements();
	static int sizeInBytes();
	static D3D11_INPUT_ELEMENT_DESC s_layout[];
};
