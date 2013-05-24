#include "VertexPosition4fColor4f.h"

VertexPosition4fColor4f::VertexPosition4fColor4f()
{

}

VertexPosition4fColor4f::VertexPosition4fColor4f( float x, float y, float z, float r, float g, float b ) :

	position( x, y, z, 1 ),
	color( r, g, b, 1 )
{

}

VertexPosition4fColor4f::VertexPosition4fColor4f( float x, float y, float z, float w, float r, float g, float b, float a ) :

	position( x, y, z, w ),
	color( r, g, b, a )
{

}

VertexPosition4fColor4f::VertexPosition4fColor4f( const Vector3f& _position, const Vector4f& _color ) :

	position( _position, 1.f ),
	color( _color )
{

}

VertexPosition4fColor4f::VertexPosition4fColor4f( const Vector4f& _position, const Vector4f& _color ) :

	position( _position ),
	color( _color )
{

}

// static
int VertexPosition4fColor4f::numElements()
{
	return 2;
}

// static
int VertexPosition4fColor4f::sizeInBytes()
{
	return 8 * sizeof( float );
}

// static
D3D11_INPUT_ELEMENT_DESC VertexPosition4fColor4f::s_layout[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 4 * sizeof( float ), D3D11_INPUT_PER_VERTEX_DATA, 0 }
};
