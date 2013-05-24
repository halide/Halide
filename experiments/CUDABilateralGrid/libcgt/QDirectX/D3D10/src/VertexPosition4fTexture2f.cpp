#include "VertexPosition4fTexture2f.h"

VertexPosition4fTexture2f::VertexPosition4fTexture2f()
{

}

VertexPosition4fTexture2f::VertexPosition4fTexture2f( float x, float y, float z, float w, float u, float v ) :

	m_position( x, y, z, w ),
	m_texture( u, v )

{

}

VertexPosition4fTexture2f::VertexPosition4fTexture2f( Vector4f position, Vector2f texture ) :

	m_position( position ),
	m_texture( texture )

{

}

// static
int VertexPosition4fTexture2f::numElements()
{
	return 2;
}

// static
int VertexPosition4fTexture2f::sizeInBytes()
{
	return 6 * sizeof( float );
}

// static
D3D10_INPUT_ELEMENT_DESC VertexPosition4fTexture2f::s_layout[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 4 * sizeof( float ), D3D10_INPUT_PER_VERTEX_DATA, 0 }
};
