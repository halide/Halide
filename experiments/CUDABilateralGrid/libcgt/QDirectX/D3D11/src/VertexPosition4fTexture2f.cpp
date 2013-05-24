#include "VertexPosition4fTexture2f.h"

VertexPosition4fTexture2f::VertexPosition4fTexture2f()
{

}

VertexPosition4fTexture2f::VertexPosition4fTexture2f( float x, float y, float z, float w, float u, float v ) :

	position( x, y, z, w ),
	texture( u, v )

{

}

VertexPosition4fTexture2f::VertexPosition4fTexture2f( const Vector4f& _position, const Vector2f& _texture ) :

	position( _position ),
	texture( _texture )

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
D3D11_INPUT_ELEMENT_DESC VertexPosition4fTexture2f::s_layout[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 4 * sizeof( float ), D3D11_INPUT_PER_VERTEX_DATA, 0 }
};
