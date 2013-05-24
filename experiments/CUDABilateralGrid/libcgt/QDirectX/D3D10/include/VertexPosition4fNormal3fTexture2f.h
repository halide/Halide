#ifndef VERTEX_POSITION4F_NORMAL3F_TEXTURE2F_H
#define VERTEX_POSITION4F_NORMAL3F_TEXTURE2F_H

#include <d3d10_1.h>
#include <d3d10.h>
#include <vecmath/Vector2f.h>
#include <vecmath/Vector3f.h>
#include <vecmath/Vector4f.h>

struct VertexPosition4fNormal3fTexture2f
{
	VertexPosition4fNormal3fTexture2f();
	VertexPosition4fNormal3fTexture2f( Vector4f position, Vector3f normal, Vector2f texture);

	Vector4f m_position;
	Vector3f m_normal;
	Vector2f m_texture;

	static int numElements();
	static int sizeInBytes();
	static D3D10_INPUT_ELEMENT_DESC s_layout[];
};

#endif // VERTEX_POSITION4F_NORMAL3F_TEXTURE2F_H
