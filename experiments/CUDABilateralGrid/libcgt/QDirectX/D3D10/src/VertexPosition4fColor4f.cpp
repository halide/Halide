#include "VertexPosition4fColor4f.h"

VertexPosition4fColor4f::VertexPosition4fColor4f()
{

}

VertexPosition4fColor4f::VertexPosition4fColor4f( float x, float y, float z, float r, float g, float b ) :
	m_position( x, y, z, 1 ),
	m_color( r, g, b, 1 )
{

}

VertexPosition4fColor4f::VertexPosition4fColor4f( float x, float y, float z, float w, float r, float g, float b, float a ) :
	m_position( x, y, z, w ),
	m_color( r, g, b, a )
{

}

VertexPosition4fColor4f::VertexPosition4fColor4f( Vector3f position, Vector4f color ) :
	m_position( position, 1.f ),
	m_color( color )
{

}

VertexPosition4fColor4f::VertexPosition4fColor4f( Vector4f position, Vector4f color ) :
	m_position( position ),
	m_color( color )
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
D3D10_INPUT_ELEMENT_DESC VertexPosition4fColor4f::s_layout[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
	{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 4 * sizeof( float ), D3D10_INPUT_PER_VERTEX_DATA, 0 }
};
