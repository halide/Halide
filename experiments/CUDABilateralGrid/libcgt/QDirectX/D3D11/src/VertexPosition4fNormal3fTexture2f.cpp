#include "VertexPosition4fNormal3fTexture2f.h"

VertexPosition4fNormal3fTexture2f::VertexPosition4fNormal3fTexture2f()
{

}

VertexPosition4fNormal3fTexture2f::VertexPosition4fNormal3fTexture2f( const Vector4f& _position, const Vector3f& _normal, const Vector2f& _texture ) :

	position( _position ),
	normal( _normal ),
	texture( _texture )
{

}    

// static
int VertexPosition4fNormal3fTexture2f::numElements()
{
	return 3;
}

// static
int VertexPosition4fNormal3fTexture2f::sizeInBytes()
{
	return 9 * sizeof( float );
}

// static
D3D11_INPUT_ELEMENT_DESC VertexPosition4fNormal3fTexture2f::s_layout[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 4 * sizeof( float ), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 7 * sizeof( float ), D3D11_INPUT_PER_VERTEX_DATA, 0 }
};
