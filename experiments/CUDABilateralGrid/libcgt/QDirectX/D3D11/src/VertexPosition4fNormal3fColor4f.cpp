#include "VertexPosition4fNormal3fColor4f.h"

VertexPosition4fNormal3fColor4f::VertexPosition4fNormal3fColor4f()
{

}

VertexPosition4fNormal3fColor4f::VertexPosition4fNormal3fColor4f( const Vector4f& _position, const Vector3f& _normal, const Vector4f& _color ) :

	position( _position ),
	normal( _normal ),
	color( _color )
{

}    

// static
int VertexPosition4fNormal3fColor4f::numElements()
{
	return 3;
}

// static
int VertexPosition4fNormal3fColor4f::sizeInBytes()
{
	return 11 * sizeof( float );
}

// static
D3D11_INPUT_ELEMENT_DESC VertexPosition4fNormal3fColor4f::s_layout[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 4 * sizeof( float ), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 7 * sizeof( float ), D3D11_INPUT_PER_VERTEX_DATA, 0 }
};
